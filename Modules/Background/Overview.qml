import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Services.Compositor
import qs.Services.Power
import qs.Services.UI

Loader {
  active: CompositorService.isNiri && Settings.data.wallpaper.enabled && Settings.data.wallpaper.overviewEnabled && (!PowerProfileService.noctaliaPerformanceMode || !Settings.data.noctaliaPerformance.disableWallpaper)

  sourceComponent: Variants {
    model: Quickshell.screens

    delegate: PanelWindow {
      id: root

      required property ShellScreen modelData

      readonly property color tintColor: Settings.data.colorSchemes.darkMode ? Color.mSurface : Color.mOnSurface

      visible: transitionLayer.wallpaperReady
      color: "transparent"
      screen: modelData
      WlrLayershell.layer: WlrLayer.Background
      WlrLayershell.exclusionMode: ExclusionMode.Ignore
      WlrLayershell.namespace: "noctalia-overview-" + (screen?.name || "unknown")

      anchors {
        top: true
        bottom: true
        right: true
        left: true
      }

      Component.onCompleted: {
        if (modelData) {
          Logger.d("Overview", "Loading overview for Niri on", modelData.name);
        }
        setWallpaperInitial();
      }

      Connections {
        target: WallpaperService
        function onWallpaperProcessingComplete(screenName, path, cachedPath) {
          if (screenName === modelData.name) {
            transitionLayer.requestWallpaperTransition(path, cachedPath || path);
          }
        }
      }

      Connections {
        target: Settings.data.wallpaper
        function onFillModeChanged() {
          if (!transitionLayer.wallpaperReady || WallpaperService.isSolidColorPath(transitionLayer.currentSource)) {
            return;
          }
          transitionLayer.requestWallpaperTransition(transitionLayer.transitioningToOriginalPath || WallpaperService.getWallpaper(modelData.name), transitionLayer.currentSource);
        }
      }

      Item {
        id: wallpaperLayer
        anchors.fill: parent
        visible: transitionLayer.wallpaperReady

        layer.enabled: Settings.data.wallpaper.overviewBlur > 0 && !PowerProfileService.noctaliaPerformanceMode
        layer.smooth: false
        layer.effect: MultiEffect {
          blurEnabled: true
          blur: Settings.data.wallpaper.overviewBlur
          blurMax: 48
        }

        WallpaperTransitionLayer {
          id: transitionLayer
          anchors.fill: parent
        }
      }

      Rectangle {
        anchors.fill: parent
        visible: transitionLayer.wallpaperReady
        color: tintColor
        opacity: Settings.data.wallpaper.overviewTint
      }

      function setWallpaperInitial() {
        if (!WallpaperService || !WallpaperService.isInitialized) {
          Qt.callLater(setWallpaperInitial);
          return;
        }

        const wallpaperPath = WallpaperService.getWallpaper(modelData.name);
        if (!wallpaperPath) {
          return;
        }

        transitionLayer.initializeWallpaper(wallpaperPath, false);
      }
    }
  }
}
