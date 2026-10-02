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
  // States tab.
  [kStrSaveStates] = { "SAVE STATES", "ESTADOS GUARDADOS", "ESTATS DESATS", "ÉTATS SAUVEGARDÉS", "ESTADOS SALVOS" },
  [kStrEmpty] = { "- EMPTY -", "- VACÍO -", "- BUIT -", "- VIDE -", "- VAZIO -" },
  [kStrSavedNoDetails] = { "SAVED (NO DETAILS)", "GUARDADO (SIN DETALLES)", "DESAT (SENSE DETALLS)",
                           "SAUVÉ (SANS DÉTAILS)", "SALVO (SEM DETALHES)" },
  [kStrTapTwice] = { "TAP TWICE TO CONFIRM", "TOCA DOS VECES PARA CONFIRMAR", "TOCA DUES VEGADES PER CONFIRMAR",
                     "TOUCHEZ DEUX FOIS POUR CONFIRMER", "TOQUE DUAS VEZES PARA CONFIRMAR" },
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
  [kStrPause] = { "PAUSE", "PAUSA", "PAUSA", "PAUSE", "PAUSA" },
  [kStrTurbo] = { "TURBO", "TURBO", "TURBO", "TURBO", "TURBO" },
  [kStrFrameSkip] = { "FRAME SKIP", "SALTO DE FOTOGRAMAS", "SALT DE FOTOGRAMES", "SAUT D'IMAGES", "SALTO DE QUADROS" },
  [kStrAudio] = { "AUDIO", "AUDIO", "ÀUDIO", "SON", "ÁUDIO" },
  [kStrFpsOverlay] = { "FPS OVERLAY", "CONTADOR DE FPS", "COMPTADOR D'FPS", "COMPTEUR FPS", "CONTADOR DE FPS" },
  [kStrDisplay] = { "DISPLAY", "IMAGEN", "IMATGE", "IMAGE", "IMAGEM" },
  [kStrPixelPerfect] = { "PIXEL PERFECT", "PÍXEL PERFECTO", "PÍXEL PERFECTE", "PIXEL PARFAIT", "PIXEL PERFEITO" },
  [kStrScaled] = { "SCALED", "ESCALADA", "ESCALADA", "ÉTIRÉE", "ESCALADA" },
  [kStrWideView] = { "WIDE VIEW", "VISTA AMPLIA", "VISTA ÀMPLIA", "VUE LARGE", "VISTA AMPLA" },
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
  [kStrRaSummary] = { "%d/%d UNLOCKED  %u/%u POINTS", "%d/%d DESBLOQUEADOS  %u/%u PUNTOS",
                      "%d/%d DESBLOQUEJATS  %u/%u PUNTS", "%d/%d DÉBLOQUÉS  %u/%u POINTS",
                      "%d/%d DESBLOQUEADAS  %u/%u PONTOS" },
  [kStrRaCheats] = { "PAUSED: A CHEAT WAS USED (RESTART THE APP)", "PAUSADOS: SE USÓ UN TRUCO (REINICIA LA APP)",
                     "EN PAUSA: S'HA USAT UN TRUC (REINICIA L'APP)", "EN PAUSE : TRICHE UTILISÉE (RELANCEZ L'APP)",
                     "PAUSADAS: TRUQUE USADO (REINICIE O APP)" },
  [kStrRaLoading] = { "LOADING THE ACHIEVEMENT LIST...", "CARGANDO LA LISTA DE LOGROS...", "CARREGANT LA LLISTA...",
                      "CHARGEMENT DE LA LISTE...", "CARREGANDO A LISTA..." },
  [kStrRaNoList] = { "LOG IN TO SEE THE ACHIEVEMENTS", "INICIA SESIÓN PARA VER LOS LOGROS",
                     "INICIA SESSIÓ PER VEURE ELS ASSOLIMENTS", "CONNECTEZ-VOUS POUR VOIR LES SUCCÈS",
                     "ENTRE PARA VER AS CONQUISTAS" },
  [kStrRaUnlocked] = { "ACHIEVEMENT UNLOCKED!", "¡LOGRO DESBLOQUEADO!", "ASSOLIMENT DESBLOQUEJAT!", "SUCCÈS DÉBLOQUÉ !",
                       "CONQUISTA DESBLOQUEADA!" },
  [kStrRaTapHint] = { "TAP ONE FOR ITS DESCRIPTION", "TOCA UNO PARA VER SU DESCRIPCIÓN", "TOCA'N UN PER VEURE'N LA DESCRIPCIÓ",
                      "TOUCHEZ-EN UN POUR SA DESCRIPTION", "TOQUE UMA PARA VER A DESCRIÇÃO" },
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
