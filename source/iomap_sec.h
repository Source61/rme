//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_IOMAP_SEC_H_
#define RME_IOMAP_SEC_H_

#include "iomap.h"

class IOMapSec : public IOMap
{
public:
  IOMapSec(MapVersion ver) { version = ver; }
  ~IOMapSec() {}

  virtual bool loadMap(Map& map, const FileName& identifier);
  virtual bool saveMap(Map& map, const FileName& identifier);

  // Writer helpers (public for use by save helpers)
  void writeItem(std::string& buf, Item* item, bool first);
  void writeItemAttributes(std::string& buf, Item* item);

  // Coordinate packing matching CipSoft server format (serversrc/moveuse.cc:23-35)
  // Domain: [24576, 40959] x [24576, 40959] x [0, 15]
  static int32_t PackAbsoluteCoordinate(int x, int y, int z) {
    return (((x - 24576) & 0x3FFF) << 18) | (((y - 24576) & 0x3FFF) << 4) | (z & 0xF);
  }
  static void UnpackAbsoluteCoordinate(int32_t packed, int& x, int& y, int& z) {
    x = ((packed >> 18) & 0x3FFF) + 24576;
    y = ((packed >> 4) & 0x3FFF) + 24576;
    z = (packed >> 0) & 0xF;
  }

  // objects.srv item type info (replaces OTB for SEC type decisions)
  struct SecObjectInfo {
    bool isBank = false;       // ground tile
    bool isBottom = false;     // always on bottom
    bool isTop = false;        // always on top
    bool isContainer = false;  // Container or Chest flag
    bool isDoor = false;       // KeyDoor, NameDoor, LevelDoor, or QuestDoor
    bool isTeleport = false;   // TeleportAbsolute or TeleportRelative
    bool isDisguise = false;
    uint16_t disguiseTarget = 0;
  };
  static std::map<uint16_t, SecObjectInfo> objectInfo;

  // Full objects.srv data for the item editor
  struct SecObjectType {
    int typeId = 0;
    std::string name;
    std::string description;
    std::vector<std::string> flags;
    std::vector<std::pair<std::string, int>> attributes; // key=value pairs
  };
  static std::map<int, SecObjectType> objectTypes;
  static bool objectTypesLoaded;
  static std::string objectsSrvPath; // path to objects.srv for save-back
  static void loadObjectTypes(const std::string& filepath);
  static void saveObjectTypes();
  static void rebuildObjectInfo(); // rebuild SecObjectInfo from SecObjectType

  // Config: available flag/attribute names from XML
  struct SecObjectConfig {
    std::vector<std::string> flagNames;
    std::vector<std::string> attributeNames;
  };
  static SecObjectConfig objectConfig;
  static void loadObjectConfig(const std::string& dataDir);
  static void saveObjectConfig(const std::string& dataDir);

  // Monster data from .mon files
  enum SecBloodType { BLOOD_BLOOD = 0, BLOOD_SLIME, BLOOD_BONES, BLOOD_FIRE, BLOOD_ENERGY };

  enum SecMonsterFlag : uint32_t {
    FLAG_KICK_BOXES       = 1 << 0,
    FLAG_KICK_CREATURES   = 1 << 1,
    FLAG_SEE_INVISIBLE    = 1 << 2,
    FLAG_UNPUSHABLE       = 1 << 3,
    FLAG_DISTANCE_FIGHTING= 1 << 4,
    FLAG_NO_SUMMON        = 1 << 5,
    FLAG_NO_ILLUSION      = 1 << 6,
    FLAG_NO_CONVINCE      = 1 << 7,
    FLAG_NO_BURNING       = 1 << 8,
    FLAG_NO_POISON        = 1 << 9,
    FLAG_NO_ENERGY        = 1 << 10,
    FLAG_NO_HIT           = 1 << 11,
    FLAG_NO_LIFE_DRAIN    = 1 << 12,
    FLAG_NO_PARALYZE      = 1 << 13,
  };

  enum SecSpellShape { SHAPE_ACTOR = 0, SHAPE_VICTIM, SHAPE_ORIGIN, SHAPE_DESTINATION, SHAPE_ANGLE };
  enum SecSpellImpact { IMPACT_DAMAGE = 0, IMPACT_FIELD, IMPACT_HEALING, IMPACT_SPEED, IMPACT_DRUNKEN, IMPACT_STRENGTH, IMPACT_OUTFIT, IMPACT_SUMMON };

  struct SecMonsterSkill {
    std::string name;
    int actual = 0;
    int minimum = 0;
    int maximum = 0;
    int nextLevel = 0;
    int factorPercent = 0;
    int addLevel = 0;
  };

  struct SecMonsterSpell {
    SecSpellShape shape = SHAPE_ACTOR;
    int shapeParams[4] = {0, 0, 0, 0};
    int shapeParamCount = 0;
    SecSpellImpact impact = IMPACT_DAMAGE;
    int impactParams[4] = {0, 0, 0, 0};
    int impactParamCount = 0;
    int delay = 0;
  };

  struct SecMonsterLoot {
    int itemId = 0;
    int maxQuantity = 0;
    int probabilityPerMille = 0;
  };

  struct SecMonsterType {
    int raceNumber = 0;
    std::string name;
    std::string article;
    int outfitId = 0;
    int outfitColors[4] = {0, 0, 0, 0}; // head, body, legs, feet
    int outfitItemType = 0; // when outfitId == 0
    int corpse = 0;
    SecBloodType blood = BLOOD_BLOOD;
    int experience = 0;
    int summonCost = 0;
    int fleeThreshold = 0;
    int attack = 0;
    int defend = 0;
    int armor = 0;
    int poison = 0;
    int loseTarget = 0;
    int strategy[4] = {100, 0, 0, 0};
    int hitpoints = 0; // derived from skills for quick access
    uint32_t flags = 0;
    std::vector<SecMonsterSkill> skills;
    std::vector<SecMonsterSpell> spells;
    std::vector<SecMonsterLoot> inventory;
    std::vector<std::string> talk;
    std::string sourceFilePath;
  };
  static std::map<int, SecMonsterType> monsterTypes; // keyed by raceNumber
  static bool monsterTypesLoaded;
  static void loadMonsterTypes(const std::string& monDir);
  static void saveMonsterType(const SecMonsterType& mon);
  static void saveAllMonsterTypes();

  // House area data from houseareas.dat
  struct SecHouseArea {
    int areaId = 0;
    std::string name;
    int sqmPrice = 0;
    int depotNr = 0;
  };
  static std::map<int, SecHouseArea> houseAreas;
  static void loadHouseAreas(const std::string& filepath);
  static void loadHouses(Map& map, const std::string& filepath);
  static void saveHouses(Map& map, const std::string& filepath);

  // Map geometry from map.dat (absolute tile coordinates, inclusive)
  struct SecMapBounds {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    int minZ = 0, maxZ = rme::MapMaxLayer;
    bool valid = false;
  };
  static SecMapBounds mapBounds;
  static void loadMapDat(const std::string& filepath);

  // Spawn data from monster.db
  struct SecSpawnEntry {
    int raceNumber = 0;
    int x = 0, y = 0, z = 0;
    int radius = 0;
    int amount = 0;
    int regen = 0;
  };
  static std::vector<SecSpawnEntry> spawnEntries;
  static void loadMonsterDb(const std::string& filepath);

private:
  bool loadSectorFile(Map& map, const std::string& filepath, int sector_x, int sector_y, int floor_z);

  // Parser helpers
  struct ParsedItem {
    uint16_t id;
    // Attributes
    int32_t amount; // -1 = not set
    int32_t charges; // -1 = not set
    int32_t keyNumber; // -1 = not set
    int32_t keyholeNumber; // -1 = not set
    int32_t doorLevel; // -1 = not set
    int32_t doorQuestNumber; // -1 = not set
    int32_t doorQuestValue; // -1 = not set
    int32_t chestQuestNumber; // -1 = not set
    int32_t containerLiquidType; // -1 = not set
    int32_t poolLiquidType; // -1 = not set
    int32_t absTeleportDest; // INT32_MIN = not set
    int32_t responsible; // -1 = not set
    int32_t remainingExpireTime; // -1 = not set
    int32_t savedExpireTime; // -1 = not set
    int32_t remainingUses; // -1 = not set
    std::string text;
    std::string editor;
    bool hasText;
    bool hasEditor;
    std::vector<ParsedItem> children;

    ParsedItem() : id(0), amount(-1), charges(-1), keyNumber(-1), keyholeNumber(-1),
      doorLevel(-1), doorQuestNumber(-1), doorQuestValue(-1), chestQuestNumber(-1),
      containerLiquidType(-1), poolLiquidType(-1), absTeleportDest(INT32_MIN),
      responsible(-1), remainingExpireTime(-1), savedExpireTime(-1), remainingUses(-1),
      hasText(false), hasEditor(false) {}
  };

  Item* createItemFromParsed(const ParsedItem& parsed);
  bool parseItemList(const std::string& line, size_t& pos, std::vector<ParsedItem>& items);
  bool parseItem(const std::string& line, size_t& pos, ParsedItem& item);
  bool parseAttributes(const std::string& line, size_t& pos, ParsedItem& item);
  std::string readQuotedString(const std::string& line, size_t& pos);
  int32_t readNumber(const std::string& line, size_t& pos);
  std::string readIdentifier(const std::string& line, size_t& pos);
  void skipWhitespace(const std::string& line, size_t& pos);

public:
  static void loadObjectsSrv(const std::string& dataDir);
  static bool objectInfoLoaded;
};

#endif // RME_IOMAP_SEC_H_
