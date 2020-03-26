/* 
    massbus_rp_c.cpp: Implements MASSBUS device logic for moving-head RP04/05/06 drives.

    Copyright Vulcan Inc. 2020 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.

*/
#include <stdint.h>
#include <string.h>
using namespace std;

#include "storagedrive.hpp"
#include "rp_drive.hpp"
#include "rh11.hpp"
#include "massbus_device.hpp"
#include "massbus_rp.hpp"

void* WorkerInit(
    void* context)
{
    massbus_rp_c* rp = reinterpret_cast<massbus_rp_c*>(context);
    rp->Worker();
    return nullptr;
}

void* SpinInit(
    void* context)
{
    massbus_rp_c* rp = reinterpret_cast<massbus_rp_c*>(context);
    rp->Spin();
    return nullptr;
}

massbus_rp_c::massbus_rp_c(
   rh11_c* controller) :
       device_c(),
       _controller(controller),
       _selectedUnit(0),
       _desiredSector(0),
       _desiredTrack(0),
       _desiredCylinder(0),
       _workerState(WorkerState::Idle),
       _workerWakeupCond(PTHREAD_COND_INITIALIZER),
       _workerMutex(PTHREAD_MUTEX_INITIALIZER),
       _err(false),
       _ata(false),
       _ned(false),
       _attnSummary(0),
       _rmr(false)
{
    name.value="MASSBUS_RP";
    type_name.value = "massbus_rp_c";
    log_label = "MASSBUS_RP";

    _newCommand = { 0 };
  
    // Start the worker thread
    pthread_attr_t attribs;
    pthread_attr_init(&attribs);

    int status = pthread_create(
        &_workerThread,
        &attribs,
        &WorkerInit,
        reinterpret_cast<void*>(this));

    pthread_attr_t attribs2;
    pthread_attr_init(&attribs2);
    status = pthread_create(
        &_spinThread,
        &attribs2,   
        &SpinInit,
        reinterpret_cast<void*>(this));

       
}

massbus_rp_c::~massbus_rp_c()
{

}

bool 
massbus_rp_c::ImplementsRegister(
	uint32_t reg)
{
    return (reg > 0) && (reg < 16);
}

std::string 
massbus_rp_c::RegisterName(
    uint32_t reg)
{
    std::string name(_registerMetadata[reg].Name);

    return name;
}

bool 
massbus_rp_c::RegisterActiveOnDATI(
    uint32_t reg)
{
    return _registerMetadata[reg].ActiveOnDATI;
}

bool 
massbus_rp_c::RegisterActiveOnDATO(
    uint32_t reg)
{
    return _registerMetadata[reg].ActiveOnDATO;
}
 
uint16_t 
massbus_rp_c::RegisterResetValue(
    uint32_t reg)
{
    return _registerMetadata[reg].ResetValue;
}

uint16_t
massbus_rp_c::RegisterWritableBits(
    uint32_t reg)
{
    return _registerMetadata[reg].WritableBits;
}

void
massbus_rp_c::WriteRegister(
    uint32_t unit, 
    uint32_t reg, 
    uint16_t value)
{
    DEBUG("RP reg write: unit %d register 0%o value 0%o", unit, reg, value);

    if (!SelectedDrive()->IsDriveReady() && 
        reg != 0 &&    // CS1 is allowed as long as GO isn't set (will be checked in DoCommand) 
        reg != (uint32_t)Registers::AttentionSummary)
    {
        // Any attempt to modify a drive register other than Attention Summary
        // while the drive is busy is invalid.
        DEBUG("Register modification while drive busy.");
        _rmr = true;
        UpdateStatus(false, false);
        return;
    }

    switch(static_cast<Registers>(reg))
    {
        case Registers::Control:
            DoCommand(unit, value);
            break;

        case Registers::DesiredSectorTrackAddress:
            _desiredTrack = (value & 0x1f00) >> 8;
            _desiredSector = (value & 0x1f);
            UpdateDesiredSectorTrack();

            DEBUG("Desired Sector Track Address: track %d, sector %d",
                _desiredTrack,
                _desiredSector);
            break;

        case Registers::DesiredCylinderAddress:
            _desiredCylinder = value & 0x3ff;
            UpdateDesiredCylinder();

            DEBUG("Desired Cylinder Address o%o (o%o)", _desiredCylinder, value);
            break;

        case Registers::AttentionSummary:
            // Clear bits in the Attention Summary register specified in the
            // written value:
            _attnSummary &= ~(value & 0xff);
            _controller->WriteRegister(reg, _attnSummary);
            DEBUG("Attention Summary write o%o, value is now o%o", value, _attnSummary); 
            break;

        case Registers::Error1:
            // Pg. 2-20 of EK-RP056-MM-01:
            // "The register can also be written by the controller for diagnostic purposes.
            // Setting any bit in this register sets the composite error bit in the status register."
            //
            // Based on diagnostic (ZRJGE0) behavior, writing ANY value here forces an error.
            //
            DEBUG("Error 1 Reg write o%o, value is now o%o", value, _error1);
            UpdateStatus(false, true);  // Force composite error.
            break;            

        default:
            FATAL("Unimplemented RP register write.");
            break;
    }
}

void massbus_rp_c::DoCommand(
    uint32_t unit,
    uint16_t command)
{
    // check for GO bit; if unset we have nothing to do here.
    if ((command & RP_GO) == 0)
    {
        return;
    }

    FunctionCode function = static_cast<FunctionCode>((command & RP_FUNC) >> 1);
    _selectedUnit = unit;

    DEBUG("RP function 0%o, unit %o", function, _selectedUnit);

    if (!SelectedDrive()->IsConnected())
    {
        // Early return for disconnected drives;
        // set NED and ERR bits
        _err = true;
        _ata = true;
        _ned = true;  // TODO: should be done at RH11 level!
        SelectedDrive()->ClearVolumeValid();
        UpdateStatus(true, false);
        return;
    }

    if (!SelectedDrive()->IsDriveReady())
    {
        // This should never happen.
        FATAL("Command sent while not ready!");
    }

    _ned = false;
    _ata = false;

    switch(static_cast<FunctionCode>(function))
    {
        case FunctionCode::Nop:
            // Nothing.
            UpdateStatus(true, false);
            break;

        case FunctionCode::ReadInPreset:
            DEBUG("RP Read-In Preset");
            //
            // "This command sets the VV (volume valid) bit, clears the desired
            //  sector/track address register, and clears the FMT, HCI, and ECI
            //  bits in the offset register.  It is used to bootstrap the device."
            //
            SelectedDrive()->SetVolumeValid();
            SelectedDrive()->SetDriveReady(); 
            _desiredSector = 0;
            _desiredTrack = 0;
            _offset = 0;
            UpdateDesiredSectorTrack();
            UpdateOffset();
            UpdateStatus(false, false);  /* do not interrupt */
            break;

        case FunctionCode::ReadData:
        case FunctionCode::WriteData:
        case FunctionCode::WriteHeaderAndData:
        case FunctionCode::ReadHeaderAndData:
        case FunctionCode::Search:
            DEBUG("RP Read/Write Data or Search");   
            {
                // Clear the unit's DRY bit
                SelectedDrive()->ClearDriveReady();

                if (function == FunctionCode::Search)
                {     
                     SelectedDrive()->SetPositioningInProgress();
                }
 
                // Clear READY 
                UpdateStatus(false, false);

                pthread_mutex_lock(&_workerMutex);

                // Save a copy of command data for the worker to consume
                _newCommand.unit = _selectedUnit;
                _newCommand.function = function;
                _newCommand.cylinder = _desiredCylinder;
                _newCommand.track = _desiredTrack;
                _newCommand.sector = _desiredSector;
                _newCommand.bus_address = _controller->GetBusAddress();
                _newCommand.word_count = _controller->GetWordCount();
                _newCommand.ready = true;

                // Wake the worker
                pthread_cond_signal(&_workerWakeupCond);
                pthread_mutex_unlock(&_workerMutex);
            }
            break;

        default:
            FATAL("Unimplemented RP function.");
            break;
    }    

}

uint16_t 
massbus_rp_c::ReadRegister(
    uint32_t unit, 
    uint32_t reg)
{
    DEBUG("RP reg read: unit %d register 0%o", unit, reg);
  
    switch(static_cast<Registers>(reg))
    {

        case Registers::DriveType:
            return SelectedDrive()->GetDriveType() | 020000;    // Moving head (MOVE TO CONSTANT)
            break;

        case Registers::SerialNo:
            return SelectedDrive()->GetSerialNumber();
            break;

        default: 
            FATAL("Unimplemented register read %o", reg);
            break;

    } 
    return 0;
}

//
// Register update functions
//
void
massbus_rp_c::UpdateStatus(
   bool complete,
   bool diagForceError)
{

   _error1 =
       (_rmr ? 04 : 0) |
       (SelectedDrive()->GetAddressOverflow() ? 01000 : 0) |
       (SelectedDrive()->GetInvalidAddress()  ? 02000 : 0) |
       (SelectedDrive()->GetWriteLockError()  ? 04000 : 0);

   if (_error1 != 0)
   {
       _err = true;
       _ata = true;
   }
   else if (diagForceError)
   {
       _err = false;
       _ata = true;
   }
   else
   {
       _err = false;
   }

   _status =
        (SelectedDrive()->GetVolumeValid() ? 0100 : 0) |
        (SelectedDrive()->IsDriveReady() ? 0200 : 0) |
        (0400) |    // Drive preset -- always set for a single-controller disk
        (SelectedDrive()->GetReadLastSector() ? 02000 : 0) |   // Last sector read
        (SelectedDrive()->IsWriteLocked() ? 04000 : 0) |   // Write lock
        (SelectedDrive()->IsPackLoaded() ? 010000 : 0) | // Medium online
        (SelectedDrive()->IsPositioningInProgress() ? 020000 : 0) |  // PIP
        (_err ? 040000 : 0) |  // Composite error
        (_ata ? 0100000 : 0);

   DEBUG("Unit %d Status: o%o", _selectedUnit, _status);
   _controller->WriteRegister(static_cast<uint32_t>(Registers::Status), _status);
   _controller->WriteRegister(static_cast<uint32_t>(Registers::Error1), _error1);

   // Update the Attention Summary register if this disk is online:

   if (_ata && SelectedDrive()->IsConnected())
   {
       _attnSummary |= (0x1 << _selectedUnit);  // TODO: these only get set, and are latched until
                                                // manually cleared?
       DEBUG("Attention Summary is now o%o", _attnSummary); 
   }

   _controller->WriteRegister(static_cast<uint32_t>(Registers::AttentionSummary), _attnSummary);

   // Inform controller of status update.
   _controller->BusStatus(complete, SelectedDrive()->IsDriveReady(), _ata, _err, SelectedDrive()->IsConnected(), _ned);
}

void
massbus_rp_c::UpdateDesiredSectorTrack()
{
   uint16_t desiredSectorTrack = (_desiredSector | (_desiredTrack << 8));
   _controller->WriteRegister(static_cast<uint32_t>(Registers::DesiredSectorTrackAddress), desiredSectorTrack);
}

void
massbus_rp_c::UpdateDesiredCylinder()
{
   _controller->WriteRegister(static_cast<uint32_t>(Registers::DesiredCylinderAddress), _desiredCylinder);
}

void
massbus_rp_c::UpdateOffset()
{
   _controller->WriteRegister(static_cast<uint32_t>(Registers::Offset), _offset);
}

void
massbus_rp_c::UpdateCurrentCylinder()
{
   _controller->WriteRegister(static_cast<uint32_t>(Registers::CurrentCylinderAddress), 
       SelectedDrive()->GetCurrentCylinder());
}

void
massbus_rp_c::Reset()
{
    // TODO: reset all drives

    // Reset registers to their defaults
    _ata = false;
    _attnSummary = 0;
    _error1 = 0;
    _rmr = false;
    UpdateStatus(false, false);

    _desiredSector = 0;
    _desiredTrack = 0;
    UpdateDesiredSectorTrack();

    _desiredCylinder = 0;
    UpdateDesiredCylinder();

    UpdateCurrentCylinder();

    _offset = 0;
    UpdateOffset(); 

    _selectedUnit = 0;
    _newCommand.ready = false;
 
}

rp_drive_c*
massbus_rp_c::SelectedDrive()
{
    return _controller->GetDrive(_selectedUnit);
}

// Background worker function
void 
massbus_rp_c::Worker()
{
    worker_init_realtime_priority(rt_device);

    _workerState = WorkerState::Idle;
    WorkerCommand command = { 0 };

    timeout_c timeout;

    DEBUG("massbus worker started.");
    while (!workers_terminate)
    {
        switch(_workerState) 
        {
            case WorkerState::Idle:
                // Wait for work.
                pthread_mutex_lock(&_workerMutex);
                while (!_newCommand.ready) 
                {
                    pthread_cond_wait(
                        &_workerWakeupCond,
                        &_workerMutex);
                }

                // Make a local copy of the new command
                command = _newCommand; 
                _newCommand.ready = false;
                pthread_mutex_unlock(&_workerMutex);

                _workerState = WorkerState::Execute;
                break;
           
            case WorkerState::Execute:
                switch(command.function)
                {
                    case FunctionCode::ReadData:
                    {
                        DEBUG("READ CHS %d/%d/%d, %d words to address o%o",  
                            _newCommand.cylinder, 
                            _newCommand.track,
                            _newCommand.sector,
                            _newCommand.word_count,
                            _newCommand.bus_address);

                        uint16_t* buffer = nullptr;
                        if (SelectedDrive()->Read(
                                _newCommand.cylinder,
                                _newCommand.track,
                                _newCommand.sector,
                                _newCommand.word_count,
                                &buffer))
                        {
                            //
                            // Data read: do DMA transfer to memory.
                            //
                            _controller->DiskReadTransfer(
                                _newCommand.bus_address,
                                _newCommand.word_count,
                                buffer);
                            
                            // Free buffer
                            delete buffer;
                        }
                        else
                        {
                            // Read failed: 
                            DEBUG("Read failed.");
                            _ata = true;
                        }

                        // Return drive to ready state
                        SelectedDrive()->SetDriveReady();
                       
                        _workerState = WorkerState::Finish;
                    }
                    break;

                    case FunctionCode::WriteData:
                    {
                        DEBUG("WRITE CHS %d/%d/%d, %d words from address o%o",
                            _newCommand.cylinder,
                            _newCommand.track,
                            _newCommand.sector,
                            _newCommand.word_count,
                            _newCommand.bus_address);

                        //
                        // Data write: do DMA transfer from memory.
                        //
                        uint16_t* buffer = _controller->DiskWriteTransfer(
                            _newCommand.bus_address,
                            _newCommand.word_count);

                        if (!buffer || !SelectedDrive()->Write(
                                _newCommand.cylinder,
                                _newCommand.track,
                                _newCommand.sector,
                                _newCommand.word_count,
                                buffer))
                        {
                            // Write failed:
                            DEBUG("Write failed.");
                            _ata = true;
                        }

                        delete buffer;

                        // Return drive to ready state
                        SelectedDrive()->SetDriveReady();

                        _workerState = WorkerState::Finish;
                    }
                    break;
                  
                    case FunctionCode::Search:
                    {
                        DEBUG("SEARCH CHS %d/%d/%d",
                            _newCommand.cylinder,
                            _newCommand.track,
                            _newCommand.sector);

                        if (!SelectedDrive()->Search(
                                 _newCommand.cylinder,
                                 _newCommand.track,
                                 _newCommand.sector)) 
                        {
                            // Search failed
                            DEBUG("Search failed");
                        }


                        // Return to ready state, set attention bit.
                        SelectedDrive()->SetDriveReady();
                        _ata = true;
                        _workerState = WorkerState::Finish;
                    }
                    break;

                    default:
                        FATAL("Unimplemented drive function %d", command.function);
                        break;
                }

                break;

            case WorkerState::Finish:
                _workerState = WorkerState::Idle;
                SelectedDrive()->SetDriveReady();
                UpdateCurrentCylinder(); 
                UpdateStatus(true, false);
                break; 
                
        }
    }
}

void
massbus_rp_c::Spin(void)
{
    // 
    // All this worker does is simulate the spinning of the disk by
    // updating the LookAhead register periodically.  In reality there'd be a
    // different value for every drive but also in reality there'd be a gigantic
    // washing-machine-sized drive spinning aluminum disks plated with rust, so...
    //
   
    uint16_t lookAhead = 0;
    timeout_c timer;


    timer.wait_ms(2500);
    while(true)
    {
        timer.wait_ms(10);

        // We update only the sector count portion of the register.
        lookAhead = (lookAhead + 1) % 22;
    //    _controller->WriteRegister(static_cast<uint32_t>(Registers::LookAhead), lookAhead << 6);
    }
}

void 
massbus_rp_c::on_power_changed(void)
{

}

void 
massbus_rp_c::on_init_changed(void)
{
    Reset();
}
