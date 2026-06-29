//======================================================================
//
// SwgCuiHudWindowManagerGround.cpp
// copyright (c) 2004 Sony Online Entertainment
//
//======================================================================

#include "swgClientUserInterface/FirstSwgClientUserInterface.h"
#include "swgClientUserInterface/SwgCuiHudWindowManagerGround.h"

#include "clientGame/Game.h"
#include "clientGame/ContainerInterface.h"
#include "clientGame/PlayerCreatureController.h"
#include "clientGame/PlayerObject.h"
#include "clientGame/TangibleObject.h"
#include "clientGraphics/Graphics.h"
#include "clientTerrain/ClientProceduralTerrainAppearance.h"
#include "clientUserInterface/CuiActionManager.h"
#include "clientUserInterface/CuiActions.h"
#include "clientUserInterface/CuiLayer_EngineCanvas.h"
#include "clientUserInterface/CuiLayerRenderer.h"
#include "clientUserInterface/CuiInventoryManager.h"
#include "clientUserInterface/CuiManager.h"
#include "clientUserInterface/CuiMediatorFactory.h"
#include "clientUserInterface/CuiMessageBox.h"
#include "clientUserInterface/CuiPreferences.h"
#include "clientUserInterface/CuiWorkspace.h"
#include "sharedFoundation/GameControllerMessage.h"
#include "sharedMessageDispatch/Message.h"
#include "sharedMessageDispatch/Transceiver.h"
#include "sharedNetworkMessages/MessageQueueGenericValueType.h"
#include "sharedNetworkMessages/NewbieTutorialEnableHudElement.h"
#include "sharedObject/Container.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedTerrain/TerrainObject.h"
#include "swgClientUserInterface/SwgCuiAllTargetsGround.h"
#include "swgClientUserInterface/SwgCuiCybernetics.h"
#include "swgClientUserInterface/SwgCuiDroidCommand.h"
#include "swgClientUserInterface/SwgCuiGroundRadar.h"
#include "swgClientUserInterface/SwgCuiGroup.h"
#include "swgClientUserInterface/SwgCuiGroupLootLottery.h"
#include "swgClientUserInterface/SwgCuiHud.h"
#include "swgClientUserInterface/SwgCuiHudFactory.h"
#include "swgClientUserInterface/SwgCuiHudGround.h"
#include "swgClientUserInterface/SwgCuiInventory.h"
#include "swgClientUserInterface/SwgCuiMediatorTypes.h"
#include "swgClientUserInterface/SwgCuiShipChoose.h"
#include "swgClientUserInterface/SwgCuiShipView.h"
#include "swgClientUserInterface/SwgCuiStatusGround.h"
#include "swgClientUserInterface/SwgCuiSurvey.h"
#include "swgClientUserInterface/SwgCuiToolbar.h"
#include "swgSharedUtility/Attributes.def"
#include "swgSharedNetworkMessages/ResourceListForSurveyMessage.h"

#include "UIData.h"
#include "UIPage.h"
#include "UIWidget.h"

#include <cstdlib>
#include <cstring>

//======================================================================

namespace SwgCuiHudWindowManagerGroundNamespace
{
	const std::string cms_newbieTutorialEnableHudElementRadar      ("radar");
	const std::string cms_newbieTutorialEnableHudElementAll        ("all");

	typedef std::map<NetworkId /* container */, int /* number of attempts */> LotteryAttempts;
	LotteryAttempts s_lotteryAttempts;
	int const cs_numberOfFramesToAttemptToOpenLottery = 256;
	UIPage *s_vrLootVisiblePage = 0;
	UIPage *s_vrInventoryVisiblePage = 0;

	bool isTruthy(char const * text)
	{
		return text && text[0] && text[0] != '0';
	}

	bool environmentEnabledDefault(char const * name, bool defaultValue)
	{
		char const * const value = std::getenv(name);
		if (!value || !*value)
			return defaultValue;
		if (_stricmp(value, "false") == 0 || _stricmp(value, "off") == 0 || _stricmp(value, "no") == 0)
			return false;
		return isTruthy(value);
	}

	bool beginVrStockPanelCapture(CuiLayer::EngineCanvas *& canvas, int & width, int & height)
	{
		if (!environmentEnabledDefault("SWG_OG_VR", false) ||
			!environmentEnabledDefault("SWG_OG_VR_OBJECT_CONTEXT_QUADS", true))
		{
			return false;
		}

		if (!Graphics::vrBeginHudCapture(&width, &height))
			return false;

		Graphics::clearViewport(true, 0x00000000, false, 1.0f, false, 0);

		if (!canvas)
		{
			canvas = new CuiLayer::EngineCanvas(UISize(width, height));
			canvas->Attach(0);
		}
		else
		{
			canvas->SetSize(UISize(width, height));
		}
		return true;
	}

	bool beginVrWristDashboardCapture(CuiLayer::EngineCanvas *& canvas, int & width, int & height)
	{
		if (!environmentEnabledDefault("SWG_OG_VR", false) ||
			!environmentEnabledDefault("SWG_OG_VR_WRIST_DASHBOARD", true))
		{
			return false;
		}

		if (!Graphics::vrBeginWristDashboardCapture(&width, &height))
			return false;

		Graphics::clearViewport(true, 0x00000000, false, 1.0f, false, 0);

		if (!canvas)
		{
			canvas = new CuiLayer::EngineCanvas(UISize(width, height));
			canvas->Attach(0);
		}
		else
		{
			canvas->SetSize(UISize(width, height));
		}
		return true;
	}

	void renderPageAt(UIPage & page, CuiLayer::EngineCanvas & canvas, long x, long y, float scale)
	{
		bool const wasVisible = page.IsVisible();
		UIPoint const oldLocation = page.GetLocation();
		page.SetVisible(true);
		page.SetLocation(0, 0);
		canvas.PushState();
		canvas.Translate(x, y);
		canvas.Scale(scale, scale);
		page.Render(canvas);
		canvas.PopState();
		page.SetLocation(oldLocation);
		page.SetVisible(wasVisible);
	}

	void renderPageAtScale(UIPage & page, CuiLayer::EngineCanvas & canvas, long x, long y, float scaleX, float scaleY)
	{
		bool const wasVisible = page.IsVisible();
		UIPoint const oldLocation = page.GetLocation();
		page.SetVisible(true);
		page.SetLocation(0, 0);
		canvas.PushState();
		canvas.Translate(x, y);
		canvas.Scale(scaleX, scaleY);
		page.Render(canvas);
		canvas.PopState();
		page.SetLocation(oldLocation);
		page.SetVisible(wasVisible);
	}

	long mappedOrigin(long texture, long client, float scale)
	{
		return static_cast<long>(static_cast<float>(texture) - static_cast<float>(client) * scale);
	}

	void renderPageClientMapped(UIPage & page, CuiLayer::EngineCanvas & canvas, long textureX, long textureY, long clientX, long clientY, float scale, bool keepClientLocation)
	{
		bool const wasVisible = page.IsVisible();
		UIPoint const oldLocation = page.GetLocation();
		page.SetVisible(true);
		page.SetLocation(clientX, clientY);
		canvas.PushState();
		canvas.Translate(mappedOrigin(textureX, clientX, scale), mappedOrigin(textureY, clientY, scale));
		canvas.Scale(scale, scale);
		page.Render(canvas);
		canvas.PopState();
		if (!keepClientLocation)
			page.SetLocation(oldLocation);
		page.SetVisible(keepClientLocation ? true : wasVisible);
	}

	int scaledLength(long value, float scale)
	{
		return static_cast<int>(static_cast<float>(value) * scale + 0.5f);
	}

	float clampScale(float scale, float low, float high)
	{
		if (scale < low)
			return low;
		if (scale > high)
			return high;
		return scale;
	}

	void clearVrObjectContextInputRegions()
	{
		for (int i = 0; i != 4; ++i)
			Graphics::vrSubmitObjectContextInputRegion(i, 0, 0, 0, 0, 0, 0, 0, 0, false);
	}

	void hideVrStockLootInventoryPages()
	{
		if (s_vrLootVisiblePage)
			s_vrLootVisiblePage->SetVisible(false);
		if (s_vrInventoryVisiblePage)
			s_vrInventoryVisiblePage->SetVisible(false);
		s_vrLootVisiblePage = 0;
		s_vrInventoryVisiblePage = 0;
		clearVrObjectContextInputRegions();
	}

	struct VrPagePlacement
	{
		long textureX;
		long textureY;
		int textureWidth;
		int textureHeight;
		long clientX;
		long clientY;
	};

	void registerVrObjectContextPageRegion(int slot, VrPagePlacement const & placement, UIPage const & page)
	{
		UIPoint const pageWorldLocation = page.GetWorldLocation();
		UISize const pageSize = page.GetSize();
		Graphics::vrSubmitObjectContextInputRegion(
			slot,
			static_cast<int>(placement.textureX),
			static_cast<int>(placement.textureY),
			static_cast<int>(placement.textureX + placement.textureWidth),
			static_cast<int>(placement.textureY + placement.textureHeight),
			static_cast<int>(pageWorldLocation.x),
			static_cast<int>(pageWorldLocation.y),
			static_cast<int>(pageWorldLocation.x + pageSize.x),
			static_cast<int>(pageWorldLocation.y + pageSize.y),
			true);
	}

	void keepVrInventoryMediatorInteractive(SwgCuiInventory & inventory)
	{
		if (!inventory.isActive())
			inventory.activate();
		CuiManager::requestPointer(true);
		for (UIWidget * widget = &inventory.getPage(); widget; widget = widget->GetParentWidget())
		{
			widget->SetEnabled(true);
			widget->SetVisible(true);
		}
	}

	float fitScale(UISize const & size, int width, int height, long marginX, long marginY, float minScale, float maxScale)
	{
		if (size.x <= 0 || size.y <= 0)
			return 1.0f;
		float const scaleX = static_cast<float>(width - marginX) / static_cast<float>(size.x);
		float const scaleY = static_cast<float>(height - marginY) / static_cast<float>(size.y);
		float const scale = scaleX < scaleY ? scaleX : scaleY;
		return clampScale(scale, minScale, maxScale);
	}

	float environmentFloatDefault(char const * name, float defaultValue)
	{
		char const * const text = std::getenv(name);
		if (!text || !*text)
			return defaultValue;
		return static_cast<float>(atof(text));
	}

	void updateVrStockRadarPage(SwgCuiGroundRadar & radar)
	{
		CreatureObject const * const player = Game::getPlayerCreature();
		TerrainObject const * const terrainObject = TerrainObject::getInstance();
		ClientProceduralTerrainAppearance const * const terrain = terrainObject ? dynamic_cast<ClientProceduralTerrainAppearance const *>(terrainObject->getAppearance()) : 0;
		if (!player || !terrain)
			return;

		UIPage & page = radar.getPage();
		bool const wasVisible = page.IsVisible();
		if (!radar.isActive())
			radar.activate();
		page.SetVisible(true);
		radar.updateRadar(player->getPosition_w(), terrain, true);
		radar.update(0.0f);
		page.SetVisible(wasVisible);
	}

	void renderVrStockObjectContextPage(UIPage & page, CachedNetworkId const & target, int health, int healthMax, int action, int actionMax, int mind, int mindMax)
	{
		hideVrStockLootInventoryPages();

		static CachedNetworkId s_lastRenderedTarget;
		static int s_lastHealth = -1;
		static int s_lastHealthMax = -1;
		static int s_lastAction = -1;
		static int s_lastActionMax = -1;
		static int s_lastMind = -1;
		static int s_lastMindMax = -1;
		if (s_lastRenderedTarget == target &&
			s_lastHealth == health &&
			s_lastHealthMax == healthMax &&
			s_lastAction == action &&
			s_lastActionMax == actionMax &&
			s_lastMind == mind &&
			s_lastMindMax == mindMax)
		{
			return;
		}

		static CuiLayer::EngineCanvas * s_vrCanvas = 0;
		int width = 0;
		int height = 0;
		if (!beginVrStockPanelCapture(s_vrCanvas, width, height))
			return;

		UISize const pageSize = page.GetSize();
		if (pageSize.x <= 0 || pageSize.y <= 0)
		{
			Graphics::vrEndHudCapture();
			return;
		}
		float const maxScale = clampScale(environmentFloatDefault("SWG_OG_VR_SELECTED_MOB_RENDER_MAX_SCALE", 7.50f), 1.50f, 10.00f);
		float const minScale = clampScale(environmentFloatDefault("SWG_OG_VR_SELECTED_MOB_RENDER_MIN_SCALE", 2.50f), 1.00f, maxScale);
		float const fitScaleX = static_cast<float>(width - 96) / static_cast<float>(pageSize.x);
		float const fitScaleY = static_cast<float>(height - 72) / static_cast<float>(pageSize.y);
		float const scale = clampScale(fitScaleX < fitScaleY ? fitScaleX : fitScaleY, minScale, maxScale);
		int const pageWidth = scaledLength(pageSize.x, scale);
		int const pageHeight = scaledLength(pageSize.y, scale);
		long const x = (width - pageWidth) / 2;
		long const y = (height - pageHeight) / 2;
		UIPoint const pageClientLocation = page.GetWorldLocation();
		renderPageClientMapped(page, *s_vrCanvas, x > 0 ? x : 0, y > 0 ? y : 0, pageClientLocation.x, pageClientLocation.y, scale, false);
		CuiLayerRenderer::flushRenderQueue();
		Graphics::vrEndHudCapture();

		s_lastRenderedTarget = target;
		s_lastHealth = health;
		s_lastHealthMax = healthMax;
		s_lastAction = action;
		s_lastActionMax = actionMax;
		s_lastMind = mind;
		s_lastMindMax = mindMax;
	}

	void renderVrStockLootInventoryPages(CreatureObject const & corpse)
	{
		CreatureObject * const player = Game::getPlayerCreature();
		ClientObject * const corpseInventory = const_cast<ClientObject *>(corpse.getInventoryObject());
		ClientObject * const playerInventory = player ? player->getInventoryObject() : 0;
		if (!corpseInventory || !playerInventory)
			return;

		static NetworkId s_lastRequestedCorpseId(NetworkId::cms_invalid);
		static int s_missingPageFrames = 0;
		if (s_lastRequestedCorpseId != corpse.getNetworkId())
		{
			s_lastRequestedCorpseId = corpse.getNetworkId();
			s_missingPageFrames = 0;
		}

		SwgCuiInventory * const lootPage = SwgCuiInventory::findInventoryPageByContainer(corpseInventory->getNetworkId(), std::string());
		SwgCuiInventory * const playerPage = SwgCuiInventory::findInventoryPageByContainer(playerInventory->getNetworkId(), std::string());
		if (!lootPage || !playerPage)
		{
			if ((s_missingPageFrames % 30) == 0)
			{
				CuiInventoryManager::requestItemOpen(*corpseInventory, std::string(), 0, 0, true, false);
				CuiInventoryManager::requestItemOpen(*playerInventory, std::string(), 0, 0, true, false);
			}
			++s_missingPageFrames;
			clearVrObjectContextInputRegions();
			return;
		}
		s_missingPageFrames = 0;

		UIPage & lootRootPage = lootPage->getPage();
		UIPage & playerRootPage = playerPage->getPage();
		UISize const lootSize = lootRootPage.GetSize();
		UISize const playerSize = playerRootPage.GetSize();
		long const gutter = 64;
		long const totalNaturalWidth = lootSize.x + gutter + playerSize.x;
		long const naturalHeight = lootSize.y > playerSize.y ? lootSize.y : playerSize.y;

		VrPagePlacement lootPlacement;
		VrPagePlacement playerPlacement;

		Container const * const corpseContainer = ContainerInterface::getContainer(*corpseInventory);
		Container const * const playerContainer = ContainerInterface::getContainer(*playerInventory);
		int const corpseItems = corpseContainer ? corpseContainer->getNumberOfItems() : -1;
		int const playerItems = playerContainer ? playerContainer->getNumberOfItems() : -1;

		static NetworkId s_lastCorpseId(NetworkId::cms_invalid);
		static int s_lastCorpseItems = -2;
		static int s_lastPlayerItems = -2;
		static VrPagePlacement s_lastLootPlacement;
		static VrPagePlacement s_lastPlayerPlacement;
		static bool s_lastPlacementValid = false;
		if (s_lastCorpseId == corpse.getNetworkId() &&
			s_lastCorpseItems == corpseItems &&
			s_lastPlayerItems == playerItems &&
			s_lastPlacementValid)
		{
			keepVrInventoryMediatorInteractive(*lootPage);
			keepVrInventoryMediatorInteractive(*playerPage);
			lootRootPage.SetLocation(s_lastLootPlacement.clientX, s_lastLootPlacement.clientY);
			playerRootPage.SetLocation(s_lastPlayerPlacement.clientX, s_lastPlayerPlacement.clientY);
			registerVrObjectContextPageRegion(0, s_lastLootPlacement, lootRootPage);
			registerVrObjectContextPageRegion(1, s_lastPlayerPlacement, playerRootPage);
			return;
		}

		static CuiLayer::EngineCanvas * s_vrCanvas = 0;
		int width = 0;
		int height = 0;
		if (!beginVrStockPanelCapture(s_vrCanvas, width, height))
			return;

		long const layoutX = totalNaturalWidth < width ? (width - totalNaturalWidth) / 2 : 8;
		long const layoutY = naturalHeight < height ? (height - naturalHeight) / 2 : 8;
		lootPlacement.clientX = layoutX > 0 ? layoutX : 0;
		lootPlacement.clientY = layoutY > 0 ? layoutY : 0;
		lootPlacement.textureX = lootPlacement.clientX;
		lootPlacement.textureY = lootPlacement.clientY;
		lootPlacement.textureWidth = static_cast<int>(lootSize.x);
		lootPlacement.textureHeight = static_cast<int>(lootSize.y);
		playerPlacement.clientX = lootPlacement.clientX + lootSize.x + gutter;
		playerPlacement.clientY = lootPlacement.clientY;
		playerPlacement.textureX = playerPlacement.clientX;
		playerPlacement.textureY = playerPlacement.clientY;
		playerPlacement.textureWidth = static_cast<int>(playerSize.x);
		playerPlacement.textureHeight = static_cast<int>(playerSize.y);

		keepVrInventoryMediatorInteractive(*lootPage);
		keepVrInventoryMediatorInteractive(*playerPage);
		lootRootPage.SetLocation(lootPlacement.clientX, lootPlacement.clientY);
		playerRootPage.SetLocation(playerPlacement.clientX, playerPlacement.clientY);
		renderPageClientMapped(lootRootPage, *s_vrCanvas, lootPlacement.textureX, lootPlacement.textureY, lootPlacement.clientX, lootPlacement.clientY, 1.0f, true);
		renderPageClientMapped(playerRootPage, *s_vrCanvas, playerPlacement.textureX, playerPlacement.textureY, playerPlacement.clientX, playerPlacement.clientY, 1.0f, true);
		registerVrObjectContextPageRegion(0, lootPlacement, lootRootPage);
		registerVrObjectContextPageRegion(1, playerPlacement, playerRootPage);
		CuiLayerRenderer::flushRenderQueue();
		Graphics::vrEndHudCapture();

		s_vrLootVisiblePage = &lootRootPage;
		s_vrInventoryVisiblePage = &playerRootPage;
		s_lastCorpseId = corpse.getNetworkId();
		s_lastCorpseItems = corpseItems;
		s_lastPlayerItems = playerItems;
		s_lastLootPlacement = lootPlacement;
		s_lastPlayerPlacement = playerPlacement;
		s_lastPlacementValid = true;
	}

	void renderVrStockWristDashboardPages(SwgCuiStatusGround * playerStatusPage, SwgCuiGroundRadar * groundRadarMediator)
	{
		if (!playerStatusPage || !groundRadarMediator)
			return;

		static CuiLayer::EngineCanvas * s_vrCanvas = 0;
		int width = 0;
		int height = 0;
		if (!beginVrWristDashboardCapture(s_vrCanvas, width, height))
			return;

		UIPage & statsPage = playerStatusPage->getPage();
		UIPage & radarPage = groundRadarMediator->getPage();
		updateVrStockRadarPage(*groundRadarMediator);
		int const panelWidth = width / 2;
		int const panelHeight = height;
		long const marginX = 32;
		long const marginY = 32;

		UISize const statsSize = statsPage.GetSize();
		float const statsScale = fitScale(statsSize, panelWidth, panelHeight, marginX, marginY, 0.75f, 2.60f);
		int const statsWidth = scaledLength(statsSize.x, statsScale);
		int const statsHeight = scaledLength(statsSize.y, statsScale);
		long const statsX = (panelWidth - statsWidth) / 2;
		long const statsY = (panelHeight - statsHeight) / 2;
		renderPageAt(statsPage, *s_vrCanvas, statsX > 0 ? statsX : 0, statsY > 0 ? statsY : 0, statsScale);

		UISize const radarSize = radarPage.GetSize();
		float const radarScale = fitScale(radarSize, panelWidth, panelHeight, marginX, marginY, 0.75f, 2.60f);
		int const radarWidth = scaledLength(radarSize.x, radarScale);
		int const radarHeight = scaledLength(radarSize.y, radarScale);
		long const radarX = panelWidth + (panelWidth - radarWidth) / 2;
		long const radarY = (panelHeight - radarHeight) / 2;
		renderPageAt(radarPage, *s_vrCanvas, radarX > panelWidth ? radarX : panelWidth, radarY > 0 ? radarY : 0, radarScale);

		CuiLayerRenderer::flushRenderQueue();
		Graphics::vrEndWristDashboardCapture();
	}
}

using namespace SwgCuiHudWindowManagerGroundNamespace;

//----------------------------------------------------------------------

SwgCuiHudWindowManagerGround::SwgCuiHudWindowManagerGround (const SwgCuiHud & hud, CuiWorkspace & workspace) :
SwgCuiHudWindowManager(hud, workspace),
m_groundRadarMediator(NULL),
m_targetStatusPage(NULL),
m_secondaryTargetStatusPage(NULL),
m_playerStatusPage(NULL),
m_petStatusPage(NULL)
{
	UIPage * mediatorPage = NULL;

	{
		hud.getCodeDataObject (TUIPage,     mediatorPage,           "RadarPage");
		mediatorPage->SetEnabled (true);
		mediatorPage->SetEnabled (false);
		m_groundRadarMediator = new SwgCuiGroundRadar (*mediatorPage);
		m_groundRadarMediator->setSettingsAutoSizeLocation (true, true);
		m_groundRadarMediator->setStickyVisible (true);
		m_groundRadarMediator->fetch ();
		m_groundRadarMediator->activate ();
		getWorkspace().addMediator (*m_groundRadarMediator);
	}

	{
		hud.getCodeDataObject (TUIPage,     mediatorPage,           "AllTargets");
		mediatorPage->SetEnabled (false);
		SwgCuiAllTargets * const allTargets = new SwgCuiAllTargetsGround(*mediatorPage);
		allTargets->setStickyVisible (true);
		allTargets->activate ();
		getWorkspace().addMediator (*allTargets);
	} //lint !e429 custodial pointer not freed or returned.  The Workspace owns it.

	{
		hud.getCodeDataObject(TUIPage, mediatorPage, "group");
		mediatorPage->SetEnabled(false);
		SwgCuiGroup * const group = new SwgCuiGroup(*mediatorPage);
		group->setSettingsAutoSizeLocation(true, true);
		group->setStickyVisible(true);
		group->activate();
		getWorkspace().addMediator(*group);
	} //lint !e429 custodial pointer not freed or returned.  The Workspace owns it.

	{
		hud.getCodeDataObject (TUIPage, mediatorPage, "TargetsPage");
		m_targetStatusPage = new SwgCuiStatusGround(*mediatorPage, SwgCuiStatusGround::ST_intendedTarget);
		if (m_targetStatusPage != NULL)
		{
			mediatorPage->SetEnabled(false);
			mediatorPage->SetEnabled(true);

			CreatureObject * const player = Game::getPlayerCreature();
			NOT_NULL(player);
			// double tap for widget state
			m_targetStatusPage->setTarget(player);
			m_targetStatusPage->setTarget(NetworkId::cms_invalid);
			m_targetStatusPage->setSettingsAutoSizeLocation(true, true);
			m_targetStatusPage->setStickyVisible(true);
			m_targetStatusPage->fetch();
			m_targetStatusPage->activate();
			m_targetStatusPage->setShowFocusedGlowRect(false);
			getWorkspace().addMediator(*m_targetStatusPage);
		}
	}

	{
		hud.getCodeDataObject (TUIPage, mediatorPage, "Pet");
		m_petStatusPage = new SwgCuiStatusGround(*mediatorPage, SwgCuiStatusGround::ST_pet);
		if (m_petStatusPage != NULL)
		{
			mediatorPage->SetEnabled(false);
			mediatorPage->SetEnabled(true);

			CreatureObject * const player = Game::getPlayerCreature();
			NOT_NULL(player);
			// double tap for widget state
			m_petStatusPage->setTarget(player);
			m_petStatusPage->setTarget(NetworkId::cms_invalid);
			m_petStatusPage->setSettingsAutoSizeLocation(false, true);
			m_petStatusPage->setStickyVisible(true);
			m_petStatusPage->fetch();
			m_petStatusPage->activate();
			m_petStatusPage->setShowFocusedGlowRect(false);

			getWorkspace().addMediator(*m_petStatusPage);
			if(Game::getPlayerObject())
				onPetChanged(*Game::getPlayerObject());

		}
	}

	{
		hud.getCodeDataObject (TUIPage, mediatorPage, "SecondaryTargetsPage");
		m_secondaryTargetStatusPage = new SwgCuiStatusGround(*mediatorPage, SwgCuiStatusGround::ST_lookAtTarget);
		if (m_secondaryTargetStatusPage != NULL)
		{
			mediatorPage->SetEnabled(false);
			mediatorPage->SetEnabled(true);

			CreatureObject * const player = Game::getPlayerCreature();
			NOT_NULL(player);
			// double tap for widget state
			m_secondaryTargetStatusPage->setTarget(player);
			m_secondaryTargetStatusPage->setTarget(NetworkId::cms_invalid);
			m_secondaryTargetStatusPage->setSettingsAutoSizeLocation(true, true);
			m_secondaryTargetStatusPage->setStickyVisible(true);
			m_secondaryTargetStatusPage->fetch();
			m_secondaryTargetStatusPage->activate();
			m_secondaryTargetStatusPage->setShowFocusedGlowRect(false);
			getWorkspace().addMediator(*m_secondaryTargetStatusPage);
		}
	}

	UIPage * statusPage = 0;
	hud.getCodeDataObject(TUIPage, statusPage, "MfdStatusPage");
	statusPage->SetVisible(false);

	connectToMessage(ResourceListForSurveyMessage::MessageType);
	connectToMessage(SurveyMessage::MessageType);
	connectToMessage(ResourceListForSurveyMessage::MessageType);
	connectToMessage(Game::Messages::SCENE_CHANGED);
}

//----------------------------------------------------------------------

SwgCuiHudWindowManagerGround::~SwgCuiHudWindowManagerGround ()
{
	getWorkspace().removeMediator(*m_groundRadarMediator);
	m_groundRadarMediator->release();
	m_groundRadarMediator = 0;

	getWorkspace().removeMediator(*m_targetStatusPage);
	m_targetStatusPage->release();
	m_targetStatusPage = 0;

	getWorkspace().removeMediator(*m_secondaryTargetStatusPage);
	m_secondaryTargetStatusPage->release();
	m_secondaryTargetStatusPage = 0;

	getWorkspace().removeMediator(*m_petStatusPage);
	m_petStatusPage->release();
	m_petStatusPage = 0;

	removePlayerStatusPage();
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnSurvey ()
{
	CuiMediator * const mediator = getWorkspace().findMediatorByType (typeid (SwgCuiSurvey));

	if (mediator)
	{
		toggleMediator (*mediator);
	}
	else
	{
		SwgCuiSurvey * const survey = SwgCuiSurvey::createInto (getWorkspace().getPage ());
		survey->setSettingsAutoSizeLocation (true, true);
		getWorkspace().addMediator (*survey);
		survey->activate ();
		getWorkspace().focusMediator (*survey, true);
		survey->setEnabled (true);
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnShipChoose(NetworkId const & terminalId) const
{
	if(Game::isSpace())
		return;

	SwgCuiShipChoose * const mediator = safe_cast<SwgCuiShipChoose * const>(CuiMediatorFactory::activateInWorkspace (CuiMediatorTypes::WS_ShipChoose));
	if(mediator)
		mediator->setTerminal(terminalId);
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnShipView(NetworkId const & shipId, NetworkId const & terminalId) const
{
	if(Game::isSpace())
		return;

	Object * const o = NetworkIdManager::getObjectById(shipId);
	ClientObject * const co = o ? o->asClientObject() : NULL;
	ShipObject * const ship = co ? co->asShipObject() : NULL;
	if(ship)
	{
		SwgCuiShipView * const mediator = safe_cast<SwgCuiShipView * const>(CuiMediatorFactory::activateInWorkspace (CuiMediatorTypes::WS_ShipView));
		if(mediator)
		{
			mediator->setShip(ship);
			mediator->setTerminal(terminalId);
		}
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnDroidCommand(NetworkId const & droidControlDeviceId) const
{
	Object * const o = NetworkIdManager::getObjectById(droidControlDeviceId);
	ClientObject * const droidControlDeviceClientObject = o ? o->asClientObject() : NULL;
	if(droidControlDeviceClientObject)
	{
		SwgCuiDroidCommand * const mediator = safe_cast<SwgCuiDroidCommand * const>(CuiMediatorFactory::activateInWorkspace (CuiMediatorTypes::WS_DroidCommand));
		if(mediator)
		{
			mediator->setDroidControlDevice(*droidControlDeviceClientObject);
		}
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnGroupLootLottery(NetworkId const & container)
{
	Object * const containerObject = NetworkIdManager::getObjectById(container);
	ClientObject * const containerClientObject = (containerObject != 0) ? containerObject->asClientObject() : 0;

	if (containerClientObject != 0)
	{
		LotteryAttempts::iterator ii = s_lotteryAttempts.find(container);
		if (ii != s_lotteryAttempts.end())
		{
			IGNORE_RETURN(s_lotteryAttempts.erase(ii));
		}

		SwgCuiGroupLootLottery * const mediator = SwgCuiGroupLootLottery::createInto(getWorkspace().getPage(), *containerClientObject);

		if (mediator != 0)
		{
			getWorkspace().addMediator(*mediator);
			mediator->fetch();
			mediator->activate();
			mediator->setEnabled(true);
			getWorkspace().focusMediator(*mediator, true);
		}

		NOT_NULL(mediator);
	}
	else
	{
		int const attempts = s_lotteryAttempts[container];

		if (attempts > cs_numberOfFramesToAttemptToOpenLottery)
		{
			LotteryAttempts::iterator ii = s_lotteryAttempts.find(container);
			if (ii != s_lotteryAttempts.end())
			{
				IGNORE_RETURN(s_lotteryAttempts.erase(ii));
			}
		}
		else
		{
			s_lotteryAttempts[container] = attempts + 1;

			CreatureObject * const player = Game::getPlayerCreature();
			Controller * const controller = (player != 0) ? player->getController() : 0;

			if (controller != 0)
			{
				typedef MessageQueueGenericValueType<NetworkId> Message;
				Message * const message = new Message(container);

				controller->appendMessage(
					CM_groupOpenLotteryWindowOnClient,
					0.0f,
					message,
					GameControllerMessageFlags::SEND |
					GameControllerMessageFlags::RELIABLE |
					GameControllerMessageFlags::DEST_AUTH_CLIENT);
			}
		}
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::closeGroupLootLottery(NetworkId const & container)
{
	Object * const containerObject = NetworkIdManager::getObjectById(container);
	ClientObject * const containerClientObject = (containerObject != 0) ? containerObject->asClientObject() : 0;

	if (containerClientObject != 0)
	{
		// close if it exists
		IGNORE_RETURN(SwgCuiGroupLootLottery::closeForContainer(*containerClientObject));
	}
} //lint !e1762 // logically nonconst

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::spawnCybernetics(NetworkId const & npc, MessageQueueCyberneticsOpen::OpenType const openType) const
{
	SwgCuiCybernetics * const mediator = safe_cast<SwgCuiCybernetics * const>(CuiMediatorFactory::activateInWorkspace (CuiMediatorTypes::WS_Cybernetics));
	if(mediator)
	{
		mediator->setNPC(npc);
		mediator->setPageOpenType(openType);
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::receiveMessage(const MessageDispatch::Emitter & emitter, const MessageDispatch::MessageBase & message)
{	
	if (message.isType (ResourceListForSurveyMessage::MessageType))
	{
		spawnSurvey();
		Archive::ReadIterator ri = NON_NULL (safe_cast<const GameNetworkMessage *>(&message))->getByteStream().begin();
		const ResourceListForSurveyMessage rlfsm(ri);
		SwgCuiSurvey * const survey = safe_cast<SwgCuiSurvey *>(getWorkspace().findMediatorByType (typeid (SwgCuiSurvey)));
		if(survey)
			survey->setResourceData(rlfsm);
	}

	else if  (message.isType (SurveyMessage::MessageType))
	{
		Archive::ReadIterator ri = NON_NULL (safe_cast<const GameNetworkMessage *>(&message))->getByteStream().begin();
		const SurveyMessage sm(ri);
		SwgCuiSurvey * const survey = safe_cast<SwgCuiSurvey *>(getWorkspace().findMediatorByType (typeid (SwgCuiSurvey)));
		if(survey)
			survey->setSurveyData(sm);
	}

	else if (message.isType (NewbieTutorialEnableHudElement::cms_name))
	{
		//-- what type of request is it?
		Archive::ReadIterator ri = NON_NULL (safe_cast<const GameNetworkMessage*> (&message))->getByteStream ().begin ();
		const NewbieTutorialEnableHudElement newbieTutorialEnableHudElement (ri);

		if (newbieTutorialEnableHudElement.getName () == cms_newbieTutorialEnableHudElementRadar || newbieTutorialEnableHudElement.getName () == cms_newbieTutorialEnableHudElementAll)
		{
			setBlinkingMediator (*m_groundRadarMediator, newbieTutorialEnableHudElement.getBlinkTime ());

			if (newbieTutorialEnableHudElement.getEnable ())
			{
				if (!m_groundRadarMediator->isActive ())
					m_groundRadarMediator->activate ();
			}
			else
			{
				if (m_groundRadarMediator->isActive ())
					m_groundRadarMediator->deactivate ();
			}
		}
	}

	else if (message.isType (Game::Messages::SCENE_CHANGED))
	{
		if (m_groundRadarMediator)
			m_groundRadarMediator->onSceneChanged ();
	}

	SwgCuiHudWindowManager::receiveMessage(emitter, message);
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::update ()
{
	const TerrainObject * const terrainObject = TerrainObject::getInstance();
	const ClientProceduralTerrainAppearance * const cmtat = terrainObject ? dynamic_cast<const ClientProceduralTerrainAppearance *> (terrainObject->getAppearance ()) : 0;			

	if (cmtat && Game::getPlayer())
	{	
		SwgCuiSurvey * const survey = safe_cast<SwgCuiSurvey *>(getWorkspace().findMediatorByType (typeid (SwgCuiSurvey)));
		if(survey && survey->isActive() && survey->hasSurvey())
		{
			survey->updateMap (*cmtat);
		}
	}

	// NOTE RHanz 4/18/07:
	// We're looking up an object by network id here.  Storing this as a CachedNetworkId is a potential
	// performance improvement.  Since NetworkIds, cached or not, are always sent across the wire as
	// NetworkIds, it *may* be sufficient to only change PlayerObject on the client side to use a
	// CachedNetworkId.

	// this block makes sure that the object that the pet target page is looking at is still valid.
	// similar blocks exist in CreatureObject for the intended/lookat targets, but those set the
	// data itself to null.  Since the petId is (currently) a one-way communication, I chose to
	// just set the mfd's target to null.

	// this should be safe.  However, design needs to make sure that all pet leashes act properly.

	if(m_petStatusPage && Game::getPlayerObject() && Game::getPlayerObject()->getPetId() != NetworkId::cms_invalid)
	{
		Object * const petObject = NetworkIdManager::getObjectById(Game::getPlayerObject()->getPetId());		
		if(!petObject)
		{
			m_petStatusPage->setTarget(NetworkId::cms_invalid);
		}
	}

	if (!m_playerStatusPage && Game::getPlayerCreature())
		createPlayerStatusPage();

	updateTargetStatusPages();
	renderVrStockWristDashboardPages(m_playerStatusPage, m_groundRadarMediator);

	SwgCuiHudWindowManager::update();
}

//----------------------------------------------------------------------

bool SwgCuiHudWindowManagerGround::isShowingLookAtTarget() const
{
	bool ret = false;

	if (m_targetStatusPage) 
	{
		ret = m_targetStatusPage->getTarget().isValid();
	}

	return ret;
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::clearLookAtTarget()
{
	if (m_targetStatusPage) 
	{
		m_targetStatusPage->clearTarget();
	}
}


//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::updateTargetStatusPages()
{
	if (m_targetStatusPage && m_secondaryTargetStatusPage)
	{
		CreatureObject const * const playerCreature = Game::getPlayerCreature();
		if (playerCreature) 
		{
			CachedNetworkId const intendedTarget = playerCreature->getIntendedTarget();
			m_targetStatusPage->setTarget(intendedTarget);
			m_targetStatusPage->getPage().SetVisible(intendedTarget.isValid());
			if (!intendedTarget.isValid())
				hideVrStockLootInventoryPages();

			int health = -1;
			int healthMax = -1;
			int action = -1;
			int actionMax = -1;
			int mind = -1;
			int mindMax = -1;
			Object const * const targetObject = intendedTarget.getObject();
			CreatureObject const * const targetCreature = CreatureObject::asCreatureObject(targetObject);
			if (targetCreature)
			{
				health = targetCreature->getAttribute(Attributes::Health);
				healthMax = targetCreature->getCurrentMaxAttribute(Attributes::Health);
				action = targetCreature->getAttribute(Attributes::Action);
				actionMax = targetCreature->getCurrentMaxAttribute(Attributes::Action);
				mind = targetCreature->getAttribute(Attributes::Mind);
				mindMax = targetCreature->getCurrentMaxAttribute(Attributes::Mind);
			}
			else
			{
				TangibleObject const * const tangible = dynamic_cast<TangibleObject const *>(targetObject);
				if (tangible)
				{
					healthMax = tangible->getMaxHitPoints();
					health = healthMax - tangible->getDamageTaken();
				}
			}

			if (targetCreature && targetCreature->isDead())
				renderVrStockLootInventoryPages(*targetCreature);
			else
				renderVrStockObjectContextPage(m_targetStatusPage->getPage(), intendedTarget, health, healthMax, action, actionMax, mind, mindMax);
			if (environmentEnabledDefault("SWG_OG_VR", false) && environmentEnabledDefault("SWG_OG_VR_OBJECT_CONTEXT_QUADS", true))
				m_targetStatusPage->getPage().SetVisible(false);

			switch (CuiPreferences::getSecondaryTargetMode())
			{
			case CuiPreferences::STM_lookAtTarget:
				m_secondaryTargetStatusPage->setTarget(playerCreature->getLookAtTarget());
				break;
			case CuiPreferences::STM_targetOfTarget:
				{
					CreatureObject const * const intendedTargetObject = CreatureObject::asCreatureObject(intendedTarget.getObject());

					if (intendedTargetObject)
						m_secondaryTargetStatusPage->setTarget(intendedTargetObject->isPlayer() ? intendedTargetObject->getIntendedTarget() : intendedTargetObject->getLookAtTarget());
					else
						m_secondaryTargetStatusPage->setTarget(NetworkId::cms_invalid);
				}
				break;
			case CuiPreferences::STM_none:
			default:
				m_secondaryTargetStatusPage->setTarget(NetworkId::cms_invalid);
				break;
			}
		}
		else
		{
			hideVrStockLootInventoryPages();
			m_targetStatusPage->setTarget(NetworkId::cms_invalid);
			m_targetStatusPage->getPage().SetVisible(false);
			m_secondaryTargetStatusPage->setTarget(NetworkId::cms_invalid);
		}
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::onTargetChanged(const CreatureObject & payload)
{
	UNREF(payload);
}

void SwgCuiHudWindowManagerGround::onPetChanged(const PlayerObject & payload)
{
	if(m_petStatusPage)
	{
		PlayerObject const * const player = Game::getPlayerObject();
		if (player && (player == &payload)) 
		{
			NetworkId const & pet = player->getPetId();
			if(pet.isValid())
			{
				m_petStatusPage->setTarget(pet);
			}
			else
			{
				m_petStatusPage->clearTarget();
			}
			setPetToolbarVisible(pet.isValid());
		}
	}
}


//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::onPlayerSetup(const CreatureObject & payload)
{
	CreatureObject const * const player = Game::getPlayerCreature();
	if (player && (player == &payload)) 
	{
		createPlayerStatusPage();
		onTargetChanged(*player);
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::onJediStateChanged(const PlayerObject & payload)
{
	PlayerObject const * const player = Game::getPlayerObject();
	if (player && (player == &payload)) 
	{
		createPlayerStatusPage();
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::createPlayerStatusPage()
{
	removePlayerStatusPage();

	CuiMediator const * const hud = SwgCuiHudFactory::findMediatorForCurrentHud();

	if (!Game::isHudSceneTypeSpace() && hud) 
	{
		PlayerObject const * const playerObject = Game::getPlayerObject();
		bool const isJedi = playerObject && playerObject->isJedi();
		
		UIPage * statusPage = NULL;
		hud->getCodeDataObject(TUIPage, statusPage, "MfdStatusPage");
		statusPage->SetVisible(false);
		
		UIPage * statusPageJedi = NULL;
		hud->getCodeDataObject(TUIPage, statusPageJedi, "MFDStatusJedi");
		statusPageJedi->SetVisible(false);
		
		UIPage * const mediatorPage = isJedi ? statusPageJedi : statusPage;
		m_playerStatusPage = new SwgCuiStatusGround(*mediatorPage, SwgCuiStatusGround::ST_player);
		if (m_playerStatusPage != NULL)
		{
			CreatureObject * const player = Game::getPlayerCreature();
			NOT_NULL(player);
			m_playerStatusPage->setTarget(player);
			mediatorPage->SetVisible(true);
			m_playerStatusPage->setSettingsAutoSizeLocation(true, true);			
			m_playerStatusPage->setStickyVisible (true);
			m_playerStatusPage->fetch();
			m_playerStatusPage->activate();
			m_playerStatusPage->setShowFocusedGlowRect(false);
			
			getWorkspace().addMediator(*m_playerStatusPage);
		}
	}

	setStatusMediator(m_playerStatusPage);
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::removePlayerStatusPage()
{
	if (m_playerStatusPage) 
	{
		getWorkspace().removeMediator(*m_playerStatusPage);
		m_playerStatusPage->release();
		m_playerStatusPage = 0;
	}
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::handlePerformActivate()
{
	SwgCuiHudWindowManager::handlePerformActivate();

	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<CreatureObject::Messages::IntendedTargetChanged*>(0));
	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<CreatureObject::Messages::LookAtTargetChanged*>(0));
	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<PlayerCreatureController::Messages::AutoAimToggled*>(0));

	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onPlayerSetup, static_cast<CreatureObject::Messages::PlayerSetup *>(0));
	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onJediStateChanged, static_cast<PlayerObject::Messages::JediStateChanged*>(0));
	m_callback->connect(*this, &SwgCuiHudWindowManagerGround::onPetChanged, static_cast<PlayerObject::Messages::PetChanged*>(0));

	if (Game::getPlayerCreature())
		onPlayerSetup(*Game::getPlayerCreature());
}

//----------------------------------------------------------------------

void SwgCuiHudWindowManagerGround::handlePerformDeactivate()
{
	SwgCuiHudWindowManager::handlePerformDeactivate();

	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<CreatureObject::Messages::IntendedTargetChanged*>(0));
	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<CreatureObject::Messages::LookAtTargetChanged*>(0));
	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onTargetChanged, static_cast<PlayerCreatureController::Messages::AutoAimToggled*>(0));

	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onPlayerSetup, static_cast<CreatureObject::Messages::PlayerSetup *>(0));
	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onJediStateChanged, static_cast<PlayerObject::Messages::JediStateChanged*>(0));
	m_callback->disconnect(*this, &SwgCuiHudWindowManagerGround::onPetChanged, static_cast<PlayerObject::Messages::PetChanged*>(0));
}

//======================================================================
