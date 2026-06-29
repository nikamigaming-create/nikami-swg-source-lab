// ======================================================================
//
// SetupSharedObject.cpp
// copyright 1998 Bootprint Entertainment
// copyright 2001 Sony Online Entertainment
//
// ======================================================================

#include "sharedObject/FirstSharedObject.h"
#include "sharedObject/SetupSharedObject.h"

#include "sharedCollision/ExtentList.h"
#include "sharedObject/AlterScheduler.h"
#include "sharedObject/Appearance.h"
#include "sharedObject/AppearanceTemplate.h"
#include "sharedObject/AppearanceTemplateList.h"
#include "sharedObject/ArrangementDescriptorList.h"
#include "sharedObject/BasicRangedIntCustomizationVariable.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/ConfigSharedObject.h"
#include "sharedObject/ContainedByProperty.h"
#include "sharedObject/CustomizationData.h"
#include "sharedObject/CustomizationIdManager.h"
#include "sharedObject/MemoryBlockManagedObject.h"
#include "sharedObject/MovementTable.h"
#include "sharedObject/NoRenderAppearanceTemplate.h"
#include "sharedObject/Object.h"
#include "sharedObject/ObjectTemplate.h"
#include "sharedObject/ObjectTemplateList.h"
#include "sharedObject/PaletteColorCustomizationVariable.h"
#include "sharedObject/PortalPropertyTemplate.h"
#include "sharedObject/ScheduleData.h"
#include "sharedObject/SlotDescriptorList.h"
#include "sharedObject/SlotIdManager.h"
#include "sharedObject/SlottedContainmentProperty.h"
#include "sharedObject/TrackingDynamics.h"
#include "sharedObject/VolumeContainmentProperty.h"
#include "sharedDebug/InstallTimer.h"

#include <cstdio>
#include <string>

// ======================================================================
// class SetupSharedObject::Data
// ======================================================================

SetupSharedObject::Data::Data()
:	version(0),
	useContainers(false),
	slotDefinitionFilename(new std::string("")),
	loadAssociatedHardpointNames(false),
	useMovementTable(false),
	movementStateTableFilename(new std::string("")),
	useTimedAppearanceTemplates(false),
	ensureDefaultAppearanceExists(true),
	customizationIdManagerFilename(0),
	objectsAlterChildrenAndContents(true),
	loadObjectTemplateCrcStringTable(true),
	pobEjectionTransformFilename(NULL)
{
}

// ----------------------------------------------------------------------

SetupSharedObject::Data::~Data()
{
	delete customizationIdManagerFilename;
	delete movementStateTableFilename;
	delete slotDefinitionFilename;
}

// ======================================================================
// class SetupSharedObject
// ======================================================================

namespace
{
	void writeStartupTrace(char const *stage)
	{
#ifdef _WIN32
		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		char line[256];
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "SetupSharedObject:%s\r\n", stage ? stage : "unknown");
		if (count > 0)
		{
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
#else
		UNREF(stage);
#endif
	}
}

// ======================================================================
/**
 * Install sharedObject.
 *
 * The settings in the Data structure will determine which subsystems
 * get initialized.
 */

void SetupSharedObject::install(const Data &data)
{
	InstallTimer const installTimer("SetupSharedObject::install");
	writeStartupTrace("install-entry");

	DEBUG_FATAL(data.version != DATA_VERSION, ("SetupSharedObject::install wrong version %d/%d", data.version, DATA_VERSION));

	ConfigSharedObject::install();
	writeStartupTrace("after-config");

	ScheduleData::install();
	writeStartupTrace("after-schedule-data");

	Appearance::install();
	writeStartupTrace("after-appearance");
	AppearanceTemplateList::install(data.useTimedAppearanceTemplates, data.ensureDefaultAppearanceExists);
	writeStartupTrace("after-appearance-template-list");
	NoRenderAppearanceTemplate::install();
	writeStartupTrace("after-no-render-appearance-template");

	// this is needed by plug-ins that aren't using the rest of the 3d system
	ExtentList::install();
	writeStartupTrace("after-extent-list");
	ObjectTemplateList::install(data.loadObjectTemplateCrcStringTable);
	writeStartupTrace("after-object-template-list");
	ObjectTemplate::install();
	writeStartupTrace("after-object-template");
	Object::install(data.objectsAlterChildrenAndContents);
	writeStartupTrace("after-object");
	MemoryBlockManagedObject::install();
	writeStartupTrace("after-memory-block-managed-object");
	CellProperty::install();
	writeStartupTrace("after-cell-property");
	AppearanceTemplate::install();
	writeStartupTrace("after-appearance-template");
	ContainedByProperty::install();
	writeStartupTrace("after-contained-by-property");
	SlottedContainmentProperty::install();
	writeStartupTrace("after-slotted-containment-property");
	VolumeContainmentProperty::install();
	writeStartupTrace("after-volume-containment-property");

	CustomizationData::install();
	writeStartupTrace("after-customization-data");
	BasicRangedIntCustomizationVariable::install();
	writeStartupTrace("after-basic-ranged-int-customization-variable");
	PaletteColorCustomizationVariable::install();
	writeStartupTrace("after-palette-color-customization-variable");
	PortalPropertyTemplate::install(data.pobEjectionTransformFilename);
	writeStartupTrace("after-portal-property-template");

	// Dynamics.
	TrackingDynamics::install();
	writeStartupTrace("after-tracking-dynamics");

	// install container-related systems
	if (data.useContainers)
	{
		DEBUG_FATAL(data.slotDefinitionFilename->empty(), ("must specify a slotDefinitionFilename if you're using containers\n"));

		writeStartupTrace("before-slot-id-manager");
		SlotIdManager::install(*data.slotDefinitionFilename, data.loadAssociatedHardpointNames);
		writeStartupTrace("after-slot-id-manager");
		SlotDescriptorList::install();
		writeStartupTrace("after-slot-descriptor-list");
		ArrangementDescriptorList::install();
		writeStartupTrace("after-arrangement-descriptor-list");
	}

	// install movement table
	if (data.useMovementTable)
	{
		DEBUG_FATAL(data.movementStateTableFilename->empty(), ("must specify a movementStateTableFilename if using the movement table\n"));
		writeStartupTrace("before-movement-table");
		MovementTable::install(*data.movementStateTableFilename);
		writeStartupTrace("after-movement-table");
	}

	writeStartupTrace("before-alter-scheduler");
	AlterScheduler::install();
	writeStartupTrace("after-alter-scheduler");

	// Optionally install customization id manager
	writeStartupTrace("before-customization-id-manager-check");
	if (data.customizationIdManagerFilename)
	{
		writeStartupTrace("before-customization-id-manager");
		CustomizationIdManager::install(data.customizationIdManagerFilename->c_str());
		writeStartupTrace("after-customization-id-manager");
	}

	writeStartupTrace("install-exit");
}

// ----------------------------------------------------------------------

void SetupSharedObject::setupDefaultGameData(Data &data)
{
	data.version = DATA_VERSION;
}

// ----------------------------------------------------------------------

void SetupSharedObject::setupDefaultConsoleData(Data &data)
{
	data.version = DATA_VERSION;
	data.loadObjectTemplateCrcStringTable = false;
}

// ----------------------------------------------------------------------

void SetupSharedObject::setupDefaultMFCData(Data &data)
{
	data.version = DATA_VERSION;
}

// ----------------------------------------------------------------------

void SetupSharedObject::addSlotIdManagerData(Data &data, bool loadAssociatedHardpointNames)
{
	data.useContainers                = true;
	*data.slotDefinitionFilename      = "abstract/slot/slot_definition/slot_definitions.iff";
	data.loadAssociatedHardpointNames = loadAssociatedHardpointNames;
}

// ----------------------------------------------------------------------

void SetupSharedObject::addMovementTableData(Data &data)
{
	data.useMovementTable            = true;
	*data.movementStateTableFilename = "datatables/movement/movementstates.iff";
}

// ----------------------------------------------------------------------

void SetupSharedObject::addCustomizationSupportData(Data &data)
{
	IS_NULL(data.customizationIdManagerFilename);
	data.customizationIdManagerFilename = new std::string("customization/customization_id_manager.iff");
}

// ----------------------------------------------------------------------

void SetupSharedObject::addPobEjectionTransformData(Data &data)
{
	IS_NULL(data.pobEjectionTransformFilename);
	data.pobEjectionTransformFilename = "datatables/pob/pob_ejection_point.iff";
}

// ======================================================================

