#include "ui_lang.h"

#include <stddef.h>

#ifdef __3DS__
#include <3ds.h>
#endif

UiLang g_ui_lang = kLangEn;

// One row per string: English, Spanish, Catalan, French, Portuguese. Format strings keep
// the English one's conversions in the same order.
static const char *const kText[kStrCount][kLangCount] = {
  [kStrOn] = { "ON", "SÍ", "SÍ", "OUI", "SIM" },
  [kStrOff] = { "OFF", "NO", "NO", "NON", "NÃO" },
  // Map tab: the follow button ("FOLLOW: ON"), the area line ("CRATERIA  12/80 CELLS  MAP").
  [kStrFollow] = { "FOLLOW", "SEGUIR", "SEGUIR", "SUIVRE", "SEGUIR" },
  [kStrCells] = { "CELLS", "CELDAS", "CEL·LES", "CASES", "CÉLULAS" },
  [kStrMapMark] = { "MAP", "MAPA", "MAPA", "CARTE", "MAPA" },
  // Status tab.
  [kStrEnergy] = { "ENERGY", "ENERGÍA", "ENERGIA", "ÉNERGIE", "ENERGIA" },
  [kStrMax] = { "MAX", "MÁX", "MÀX", "MAX", "MÁX" },
  [kStrReserve] = { "RESERVE", "RESERVA", "RESERVA", "RÉSERVE", "RESERVA" },
  [kStrAuto] = { "AUTO", "AUTO", "AUTO", "AUTO", "AUTO" },
  [kStrManual] = { "MANUAL", "MANUAL", "MANUAL", "MANUEL", "MANUAL" },
  [kStrItems] = { "ITEMS", "OBJETOS", "OBJECTES", "OBJETS", "ITENS" },
  [kStrBeams] = { "BEAMS", "RAYOS", "RAIGS", "RAYONS", "RAIOS" },
  [kStrMapStations] = { "MAP STATIONS", "ESTACIONES DE MAPA", "ESTACIONS DE MAPA", "STATIONS DE CARTE",
                        "ESTAÇÕES DE MAPA" },
  [kStrRoom] = { "ROOM", "SALA", "SALA", "SALLE", "SALA" },
  // States tab.
  [kStrSaveStates] = { "SAVE STATES", "ESTADOS GUARDADOS", "ESTATS DESATS", "ÉTATS SAUVEGARDÉS", "ESTADOS SALVOS" },
  [kStrSavedNoDetails] = { "SAVED (NO DETAILS)", "GUARDADO (SIN DETALLES)", "DESAT (SENSE DETALLS)",
                           "SAUVÉ (SANS DÉTAILS)", "SALVO (SEM DETALHES)" },
  [kStrSavedSlot] = { "Saved to slot %d", "Guardado en la ranura %d", "Desat a la ranura %d",
                      "Sauvé dans l'emplacement %d", "Salvo no espaço %d" },
  [kStrSaveFailed] = { "Could not save slot %d", "No se pudo guardar la ranura %d", "No s'ha pogut desar la ranura %d",
                       "Échec de la sauvegarde %d", "Não foi possível salvar o espaço %d" },
  [kStrLoadedSlot] = { "Loaded slot %d", "Ranura %d cargada", "Ranura %d carregada", "Emplacement %d chargé",
                       "Espaço %d carregado" },
  [kStrLoadFailed] = { "Slot %d: cannot load", "Ranura %d: no se puede cargar", "Ranura %d: no es pot carregar",
                       "Emplacement %d : chargement impossible", "Espaço %d: não foi possível carregar" },
  [kStrGameReset] = { "Game reset", "Partida reiniciada", "Partida reiniciada", "Partie réinitialisée",
                      "Jogo reiniciado" },
  // Options tab (a cell's label fits 23 characters).
  [kStrPacing] = { "FRAMES", "FOTOGRAMAS", "FOTOGRAMES", "IMAGES", "QUADROS" },
  [kStrAudio] = { "AUDIO", "AUDIO", "ÀUDIO", "SON", "ÁUDIO" },
  [kStrDisplay] = { "DISPLAY", "IMAGEN", "IMATGE", "IMAGE", "IMAGEM" },
  [kStrPixelP] = { "PIXEL P.", "PÍXEL P.", "PÍXEL P.", "PIXEL P.", "PIXEL P." },
  [kStrScaled] = { "SCALED", "ESCALADA", "ESCALADA", "ÉTIRÉE", "ESCALADA" },
  [kStrView] = { "VIEW", "VISTA", "VISTA", "VUE", "VISTA" },
  [kStrLanguage] = { "LANGUAGE", "IDIOMA", "IDIOMA", "LANGUE", "IDIOMA" },
  [kStrResetGame] = { "RESET GAME", "REINICIAR PARTIDA", "REINICIAR PARTIDA", "RELANCER LA PARTIE", "REINICIAR JOGO" },
  // A toast: one line of at most 52 characters.
  [kStrFrameSkipOffToast] = { "FRAME SKIP OFF: heavy rooms may slow down",
                              "Sin salto de fotogramas: salas pesadas irán lentas",
                              "Sense salt de fotogrames: sales pesades van lentes",
                              "Sans saut d'images, les salles lourdes ralentissent",
                              "Sem salto de quadros: salas pesadas ficam lentas" },
  // The RESET GAME window.
  [kStrResetQuestion] = { "RESET THE GAME?", "¿REINICIAR LA PARTIDA?", "REINICIAR LA PARTIDA?", "RELANCER LA PARTIE ?",
                          "REINICIAR O JOGO?" },
  [kStrResetLost1] = { "PROGRESS SINCE THE LAST SAVE", "SE PIERDE EL PROGRESO DESDE", "ES PERD EL PROGRÉS DES DE",
                       "LA PROGRESSION DEPUIS LA", "O PROGRESSO DESDE O ÚLTIMO" },
  [kStrResetLost2] = { "IS LOST", "EL ÚLTIMO GUARDADO", "L'ÚLTIM DESAT", "DERNIÈRE SAUVEGARDE EST PERDUE",
                       "SAVE SERÁ PERDIDO" },
  [kStrReset] = { "RESET", "REINICIAR", "REINICIAR", "RELANCER", "REINICIAR" },
  [kStrCancel] = { "CANCEL", "CANCELAR", "CANCEL·LAR", "ANNULER", "CANCELAR" },
  // The achievements tab (RetroAchievements; titles and descriptions come from its server).
  [kStrRaAchievements] = { "ACHIEVEMENTS", "LOGROS", "ASSOLIMENTS", "SUCCÈS", "CONQUISTAS" },
  [kStrRaLogin] = { "LOG IN", "INICIAR SESIÓN", "INICIA SESSIÓ", "CONNEXION", "ENTRAR" },
  [kStrRaLogout] = { "LOG OUT", "CERRAR SESIÓN", "TANCA SESSIÓ", "DÉCONNEXION", "SAIR" },
  [kStrRaDisabled] = { "SWITCHED OFF", "DESACTIVADOS", "DESACTIVATS", "DÉSACTIVÉS", "DESATIVADAS" },
  [kStrRaNoAccount] = { "NOT LOGGED IN", "SIN INICIAR SESIÓN", "SENSE SESSIÓ", "NON CONNECTÉ", "SEM SESSÃO" },
  [kStrRaConnecting] = { "CONNECTING...", "CONECTANDO...", "CONNECTANT...", "CONNEXION...", "CONECTANDO..." },
  [kStrRaOnline] = { "ONLINE AS %s", "CONECTADO COMO %s", "CONNECTAT COM A %s", "CONNECTÉ : %s", "CONECTADO COMO %s" },
  [kStrRaOffline] = { "OFFLINE (UNLOCKS WAIT)", "SIN CONEXIÓN (LOS LOGROS ESPERAN)", "SENSE CONNEXIÓ (ELS ASSOLIMENTS ESPEREN)",
                      "HORS LIGNE (LES SUCCÈS ATTENDENT)", "OFFLINE (AS CONQUISTAS AGUARDAM)" },
  [kStrRaLoginError] = { "LOGIN REJECTED", "INICIO DE SESIÓN RECHAZADO", "INICI DE SESSIÓ REBUTJAT", "CONNEXION REFUSÉE",
                         "LOGIN RECUSADO" },
  [kStrRaSummary] = { "%d/%d  %u/%u PTS", "%d/%d  %u/%u PTS", "%d/%d  %u/%u PTS", "%d/%d  %u/%u PTS",
                      "%d/%d  %u/%u PTS" },
  [kStrRaLoading] = { "LOADING THE ACHIEVEMENT LIST...", "CARGANDO LA LISTA DE LOGROS...", "CARREGANT LA LLISTA...",
                      "CHARGEMENT DE LA LISTE...", "CARREGANDO A LISTA..." },
  [kStrRaNoList] = { "LOG IN TO SEE THE ACHIEVEMENTS", "INICIA SESIÓN PARA VER LOS LOGROS",
                     "INICIA SESSIÓ PER VEURE ELS ASSOLIMENTS", "CONNECTEZ-VOUS POUR VOIR LES SUCCÈS",
                     "ENTRE PARA VER AS CONQUISTAS" },
  [kStrRaUnlocked] = { "ACHIEVEMENT UNLOCKED!", "¡LOGRO DESBLOQUEADO!", "ASSOLIMENT DESBLOQUEJAT!", "SUCCÈS DÉBLOQUÉ !",
                       "CONQUISTA DESBLOQUEADA!" },
  [kStrRaNotify] = { "NOTICE", "AVISO", "AVÍS", "AVIS", "AVISO" },
  [kStrRaTop] = { "TOP", "ARRIBA", "A DALT", "HAUT", "CIMA" },
  [kStrRaBottom] = { "BOTTOM", "ABAJO", "A BAIX", "BAS", "BAIXO" },
  [kStrRaSound] = { "SOUND", "SONIDO", "SO", "SON", "SOM" },
  [kStrRaSortDefault] = { "LOCKED FIRST", "BLOQUEADOS PRIMERO", "BLOQUEJATS PRIMER", "VERROUILLÉS D'ABORD",
                          "BLOQUEADAS PRIMEIRO" },
  [kStrRaSortTitle] = { "NAME", "NOMBRE", "NOM", "NOM", "NOME" },
  [kStrRaSortPoints] = { "POINTS", "PUNTOS", "PUNTS", "POINTS", "PONTOS" },
  [kStrRaSortRecent] = { "RECENT", "RECIENTES", "RECENTS", "RÉCENTS", "RECENTES" },
  [kStrRaPoints] = { "%u POINTS", "%u PUNTOS", "%u PUNTS", "%u POINTS", "%u PONTOS" },
  [kStrRaLockedState] = { "LOCKED", "BLOQUEADO", "BLOQUEJAT", "VERROUILLÉ", "BLOQUEADA" },
  [kStrRaUnlockedState] = { "UNLOCKED", "DESBLOQUEADO", "DESBLOQUEJAT", "DÉBLOQUÉ", "DESBLOQUEADA" },
  [kStrRaMissable] = { "MISSABLE", "PERDIBLE", "PERDIBLE", "MANQUABLE", "PERDÍVEL" },
  [kStrRaProgression] = { "PROGRESSION", "PROGRESIÓN", "PROGRESSIÓ", "PROGRESSION", "PROGRESSÃO" },
  [kStrRaWin] = { "WIN CONDITION", "CONDICIÓN DE VICTORIA", "CONDICIÓ DE VICTÒRIA", "CONDITION DE VICTOIRE",
                  "CONDIÇÃO DE VITÓRIA" },
  [kStrClose] = { "CLOSE", "CERRAR", "TANCA", "FERMER", "FECHAR" },
  [kStrUpdate] = { "UPDATE", "ACTUALIZAR", "ACTUALITZAR", "MISE À JOUR", "ATUALIZAR" },
  [kStrUpdates] = { "UPDATES", "ACTUALIZACIONES", "ACTUALITZACIONS", "MISES À JOUR", "ATUALIZAÇÕES" },
  [kStrUpdTap] = { "TAP TO CHECK", "TOCA PARA BUSCAR", "TOCA PER CERCAR", "TOUCHER: VÉRIFIER", "TOQUE PARA VERIFICAR" },
  [kStrUpdChecking] = { "CHECKING...", "BUSCANDO...", "CERCANT...", "VÉRIFICATION...", "VERIFICANDO..." },
  [kStrUpdUpToDate] = { "UP TO DATE", "AL DÍA", "AL DIA", "À JOUR", "ATUALIZADO" },
  [kStrUpdNew] = { "NEW: %s", "NUEVA: %s", "NOVA: %s", "NOUVELLE: %s", "NOVA: %s" },
  [kStrUpdInstalled] = { "RESTART TO USE", "REINICIA PARA USAR", "REINICIA PER USAR", "REDÉMARRER", "REINICIE PARA USAR" },
  [kStrUpdError] = { "ERROR: TAP TO RETRY", "ERROR: TOCA PARA REINTENTAR", "ERROR: TOCA PER REINTENTAR", "ERREUR: RÉESSAYER", "ERRO: TENTAR DE NOVO" },
  [kStrUpdAsk] = { "NEW VERSION %s", "NUEVA VERSIÓN %s", "NOVA VERSIÓ %s", "NOUVELLE VERSION %s", "NOVA VERSÃO %s" },
  [kStrUpdAsk2] = { "INSTALL IT NOW?", "¿INSTALARLA AHORA?", "VOLS INSTAL·LAR-LA ARA?", "L'INSTALLER MAINTENANT ?", "INSTALAR AGORA?" },
  [kStrUpdInstalling] = { "INSTALLING...", "INSTALANDO...", "INSTAL·LANT...", "INSTALLATION...", "INSTALANDO..." },
  [kStrUpdRestart] = { "UPDATED. RESTART NOW?", "ACTUALIZADO. ¿REINICIAR?", "ACTUALITZAT. REINICIAR?", "MIS À JOUR. REDÉMARRER ?", "ATUALIZADO. REINICIAR?" },
  [kStrUpdFailed] = { "THE UPDATE FAILED", "LA ACTUALIZACIÓN FALLÓ", "L'ACTUALITZACIÓ HA FALLAT", "LA MISE À JOUR A ÉCHOUÉ", "A ATUALIZAÇÃO FALHOU" },
  [kStrUpdKept] = { "CIA KEPT IN UPDATE/ FOR FBI", "CIA EN UPDATE/ PARA FBI", "CIA A UPDATE/ PER A FBI", "CIA DANS UPDATE/ POUR FBI", "CIA EM UPDATE/ PARA O FBI" },
  [kStrYes] = { "YES", "SÍ", "SÍ", "OUI", "SIM" },
  [kStrNo] = { "NO", "NO", "NO", "NON", "NÃO" },
  [kStrOk] = { "OK", "OK", "OK", "OK", "OK" },
  [kStrNewState] = { "+ NEW", "+ NUEVO", "+ NOU", "+ NOUVEAU", "+ NOVO" },
  [kStrStatesNone] = { "NO STATES YET: TAP + NEW", "SIN ESTADOS: TOCA + NUEVO", "SENSE ESTATS: TOCA + NOU", "AUCUN ÉTAT: TOUCHEZ + NOUVEAU", "SEM ESTADOS: TOQUE + NOVO" },
  [kStrNoImage] = { "NO IMAGE", "SIN IMAGEN", "SENSE IMATGE", "PAS D'IMAGE", "SEM IMAGEM" },
  [kStrMark] = { "MARK", "MARCA", "MARCA", "MARQUE", "MARCA" },
  [kStrTime] = { "TIME", "TIEMPO", "TEMPS", "TEMPS", "TEMPO" },
  [kStrLoad] = { "LOAD", "CARGAR", "CARREGAR", "CHARGER", "CARREGAR" },
  [kStrSaveOver] = { "SAVE OVER", "GUARDAR", "DESAR", "ÉCRASER", "SOBREPOR" },
  [kStrDelete] = { "DELETE", "BORRAR", "ESBORRAR", "SUPPRIMER", "APAGAR" },
  [kStrStateDeleted] = { "STATE %d DELETED", "ESTADO %d BORRADO", "ESTAT %d ESBORRAT", "ÉTAT %d SUPPRIMÉ", "ESTADO %d APAGADO" },
  [kStrWait] = { "PLEASE WAIT...", "ESPERA...", "ESPERA...", "PATIENTEZ...", "AGUARDE..." },
  [kStrPaceLock] = { "LOCK 30", "30 FIJOS", "30 FIXOS", "30 FIXES", "30 FIXOS" },
  [kStrPaceNoSkip] = { "NO SKIP", "SIN SALTO", "SENSE SALT", "SANS SAUT", "SEM SALTO" },
  [kStrViewOriginal] = { "ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINALE", "ORIGINAL" },
  [kStrViewWide] = { "WIDE", "AMPLIADA", "AMPLIADA", "LARGE", "AMPLIADA" },
  [kStrChannel] = { "CHANNEL", "CANAL", "CANAL", "CANAL", "CANAL" },
  [kStrChanStable] = { "STABLE", "ESTABLES", "ESTABLES", "STABLES", "ESTÁVEIS" },
  [kStrChanBeta] = { "+ BETAS", "+ BETAS", "+ BETES", "+ BÊTAS", "+ BETAS" },
  [kStrHud] = { "HUD", "HUD", "HUD", "HUD", "HUD" },
  [kStrHudHidden] = { "HIDDEN WITH ITS TAB", "OCULTO CON SU PESTAÑA", "AMAGAT AMB LA PESTANYA", "MASQUÉ AVEC SON ONGLET", "OCULTO COM A ABA" },
  [kStrHudShown] = { "ALWAYS SHOWN", "SIEMPRE VISIBLE", "SEMPRE VISIBLE", "TOUJOURS VISIBLE", "SEMPRE VISÍVEL" },
  [kStrWhatsNew] = { "WHAT'S NEW", "NOVEDADES", "NOVETATS", "NOUVEAUTÉS", "NOVIDADES" },
  [kStrNotesEmpty] = { "NOTHING YET. CHECK NOW FIRST.", "AÚN SIN DATOS. BUSCA ACTUALIZACIÓN.",
                       "ENCARA SENSE DADES. CERCA ACTUALITZACIÓ.", "RIEN POUR L'INSTANT. VÉRIFIEZ D'ABORD.",
                       "SEM DADOS. VERIFIQUE PRIMEIRO." },
  // Short forms for the OPTIONS half button (about 11 characters).
  [kStrUpdsLabel] = { "UPDATES", "ACTUALIZ.", "ACTUALITZ.", "MAJ", "ATUALIZ." },
  [kStrUpdsCheck] = { "CHECK", "BUSCAR", "CERCAR", "VÉRIFIER", "VERIFICAR" },
  [kStrUpdsChecking] = { "CHECKING", "BUSCANDO", "CERCANT", "VÉRIF.", "VERIFIC." },
  [kStrUpdsNew] = { "NEW", "NUEVA", "NOVA", "NOUVELLE", "NOVA" },
  [kStrUpdsInstalling] = { "INSTALLING", "INSTALANDO", "INSTAL·LANT", "INSTALL.", "INSTALANDO" },
  [kStrUpdsRestart] = { "RESTART", "REINICIA", "REINICIA", "REDÉMARRER", "REINICIE" },
  [kStrUpdsError] = { "ERROR", "ERROR", "ERROR", "ERREUR", "ERRO" },
  [kStrUpdFrom] = { "%s > %s", "%s > %s", "%s > %s", "%s > %s", "%s > %s" },
};

static const char *const kNames[kLangCount] = { "ENGLISH", "ESPAÑOL", "CATALÀ", "FRANÇAIS", "PORTUGUÊS" };

const char *Tr(UiStr id) {
  if ((unsigned)id >= kStrCount) return "";
  const char *s = kText[id][(unsigned)g_ui_lang < kLangCount ? g_ui_lang : kLangEn];
  return s ? s : kText[id][kLangEn];
}

const char *UiLang_Name(UiLang lang) { return (unsigned)lang < kLangCount ? kNames[lang] : kNames[kLangEn]; }

UiLang UiLang_FromSystem(void) {
#ifdef __3DS__
  u8 lang = CFG_LANGUAGE_EN;
  if (R_SUCCEEDED(cfguInit())) {
    if (R_FAILED(CFGU_GetSystemLanguage(&lang))) lang = CFG_LANGUAGE_EN;
    cfguExit();
  }
  switch (lang) {
  case CFG_LANGUAGE_ES: return kLangEs;
  case CFG_LANGUAGE_FR: return kLangFr;
  case CFG_LANGUAGE_PT: return kLangPt;
  default: return kLangEn;
  }
#else
  return kLangEn;
#endif
}

// ---- The game's names -----------------------------------------------------------------
// Spanish and French follow Nintendo's own translations: Zero Mission (Morfosfera,
// Supersalto, Salto en barrena, Aceleración, Rayo recarga, Rayo de ondas, Traje climático,
// Bomba de energía; Méga Saut, Attaque en Vrille, Rayon à Vague...), then the later games
// (Rotosalto, Rayo enganche). Spazer has no official Spanish name: Rayo múltiple, as the
// Spanish Metroid wiki. Catalan follows the Spanish choices. Portuguese: Morfosfera (the
// Brazilian Metroid Prime 4 guides), Salto Esfera (the 2013 PT-BR fan translation).

static const char *const kItems[kLangCount][11] = {
  { "VARIA", "GRAV", "MORPH", "BOMB", "HIJUMP", "SPACE", "SPEED", "SCREW", "SPRING", "GRAPPL", "XRAY" },
  { "CLIMÁTICO", "GRAVITAT.", "MORFOSFERA", "BOMBAS", "SUPERSALTO", "SALTO ESP.", "ACELERACIÓN", "BARRENA",
    "ROTOSALTO", "ENGANCHE", "RAYOS X" },
  { "CLIMÀTIC", "GRAVITAT.", "MORFOESFERA", "BOMBES", "SUPERSALT", "SALT ESP.", "ACCELERACIÓ", "BARRINA",
    "ROTOSALT", "ENGANXADA", "RAIGS X" },
  { "VARIA", "GRAVITÉ", "MORPHING", "BOMBES", "MÉGA SAUT", "SAUT SPAT.", "ACCÉLÉR.", "VRILLE", "REBOND",
    "GRAPPIN", "RAYONS X" },
  { "VARIA", "GRAVITAC.", "MORFOSFERA", "BOMBAS", "BOTAS SALTO", "SALTO ESP.", "ACELERADOR", "GIRATÓRIO",
    "SALTO ESF.", "GANCHO", "RAIOS X" },
};

static const char *const kBeams[kLangCount][5] = {
  { "CHARGE", "ICE", "WAVE", "SPAZER", "PLASMA" },
  { "RECARGA", "HIELO", "ONDAS", "MÚLTIPLE", "PLASMA" },
  { "CÀRREGA", "GEL", "ONES", "MÚLTIPLE", "PLASMA" },
  { "CHARGE", "GLACE", "VAGUE", "SPAZER", "PLASMA" },
  { "CARGA", "GELO", "ONDA", "SPAZER", "PLASMA" },
};

static const char *const kAmmo[kLangCount][3] = {
  { "MSL", "SUPER", "PB" },
  { "MISIL", "SUPER", "BOMBA" },
  { "MÍSSIL", "SUPER", "BOMBA" },
  { "MISSILE", "SUPER", "BOMBE" },
  { "MÍSSIL", "SUPER", "BOMBA" },
};

static const char *const kAreas[kLangCount][8] = {
  { "Crateria", "Brinstar", "Norfair", "Wrecked Ship", "Maridia", "Tourian", "Ceres", "Debug" },
  { "Crateria", "Brinstar", "Norfair", "Nave Hundida", "Maridia", "Tourian", "Ceres", "Debug" },
  { "Crateria", "Brinstar", "Norfair", "Nau Nàufraga", "Maridia", "Tourian", "Ceres", "Debug" },
  { "Crateria", "Brinstar", "Norfair", "Épave", "Maridia", "Tourian", "Ceres", "Debug" },
  { "Crateria", "Brinstar", "Norfair", "Nau Afundada", "Maridia", "Tourian", "Ceres", "Debug" },
};

static const char *const kAreasShort[kLangCount][8] = {
  { "CRA", "BRI", "NOR", "WRE", "MAR", "TOU", "CER", "DBG" },
  { "CRA", "BRI", "NOR", "NAV", "MAR", "TOU", "CER", "DBG" },
  { "CRA", "BRI", "NOR", "NAU", "MAR", "TOU", "CER", "DBG" },
  { "CRA", "BRI", "NOR", "ÉPA", "MAR", "TOU", "CER", "DBG" },
  { "CRA", "BRI", "NOR", "NAV", "MAR", "TOU", "CER", "DBG" },
};

static unsigned Lang(void) { return (unsigned)g_ui_lang < kLangCount ? (unsigned)g_ui_lang : kLangEn; }

const char *TrItem(int i) { return (unsigned)i < 11 ? kItems[Lang()][i] : ""; }
const char *TrBeam(int i) { return (unsigned)i < 5 ? kBeams[Lang()][i] : ""; }
const char *TrAmmo(int i) { return (unsigned)i < 3 ? kAmmo[Lang()][i] : ""; }
const char *TrArea(int area) { return kAreas[Lang()][(unsigned)area < 8 ? area : 7]; }
const char *TrAreaShort(int area) { return kAreasShort[Lang()][(unsigned)area < 8 ? area : 7]; }
