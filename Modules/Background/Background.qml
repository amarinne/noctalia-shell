import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Services.Compositor
import qs.Services.Power
import qs.Services.UI

Variants {
  id: backgroundVariants
  model: Quickshell.screens

  delegate: Loader {

    required property ShellScreen modelData

    active: modelData && Settings.data.wallpaper.enabled && (!PowerProfileService.noctaliaPerformanceMode || !Settings.data.noctaliaPerformance.disableWallpaper)

    sourceComponent: PanelWindow {
      id: root

      visible: transitionLayer.wallpaperReady

      Component.onCompleted: setWallpaperInitial()

      Connections {
        target: Settings.data.wallpaper
        function onFillModeChanged() {
          fillMode = WallpaperService.getFillModeUniform();
        }
      }

      // External state management
      Connections {
        target: WallpaperService
        function onWallpaperChanged(screenName, path) {
          if (screenName === modelData.name) {
            requestPreprocessedWallpaper(path);
          }
        }
      }

      Connections {
        target: CompositorService
        function onDisplayScalesChanged() {
          if (!WallpaperService.isInitialized) {
            return;
          }

          const currentPath = WallpaperService.getWallpaper(modelData.name);
          if (!currentPath || WallpaperService.isSolidColorPath(currentPath)) {
            return;
          }

          if (isStartupTransition) {
            // During startup, just ensure the correct cache exists without visual changes
            const compositorScale = CompositorService.getDisplayScale(modelData.name);
            const targetWidth = Math.round(modelData.width * compositorScale);
            const targetHeight = Math.round(modelData.height * compositorScale);
            ImageCacheService.getLarge(currentPath, targetWidth, targetHeight, function (cachedPath, success) {
              WallpaperService.wallpaperProcessingComplete(modelData.name, currentPath, success ? cachedPath : "");
            });
            return;
          }

          requestPreprocessedWallpaper(currentPath);
        }
      }

      color: "transparent"
      screen: modelData
      WlrLayershell.layer: WlrLayer.Background
      WlrLayershell.exclusionMode: ExclusionMode.Ignore
      WlrLayershell.namespace: "noctalia-wallpaper-" + (screen?.name || "unknown")

      anchors {
        bottom: true
        top: true
        right: true
        left: true
      }

      WallpaperTransitionLayer {
        id: transitionLayer
        anchors.fill: parent
      }
      function setWallpaperInitial() {
        // On startup, defer assigning wallpaper until the services are ready
        if (!WallpaperService || !WallpaperService.isInitialized) {
          Qt.callLater(setWallpaperInitial);
          return;
        }
        if (!ImageCacheService || !ImageCacheService.initialized) {
          Qt.callLater(setWallpaperInitial);
          return;
        }

        // Check if we're in solid color mode
        if (Settings.data.wallpaper.useSolidColor) {
          var solidPath = WallpaperService.createSolidColorPath(Settings.data.wallpaper.solidColor.toString());
          transitionLayer.initializeWallpaper(solidPath, true);
          WallpaperService.wallpaperProcessingComplete(modelData.name, solidPath, "");
          return;
        }

        const wallpaperPath = WallpaperService.getWallpaper(modelData.name);

        // Check if the path is a solid color
        if (WallpaperService.isSolidColorPath(wallpaperPath)) {
          transitionLayer.initializeWallpaper(wallpaperPath, true);
          WallpaperService.wallpaperProcessingComplete(modelData.name, wallpaperPath, "");
          return;
        }

        const compositorScale = CompositorService.getDisplayScale(modelData.name);
        const targetWidth = Math.round(modelData.width * compositorScale);
        const targetHeight = Math.round(modelData.height * compositorScale);

        ImageCacheService.getLarge(wallpaperPath, targetWidth, targetHeight, function (cachedPath, success) {
          transitionLayer.initializeWallpaper(success ? cachedPath : wallpaperPath, true);
          // Pass cached path for blur optimization (already resized)
          WallpaperService.wallpaperProcessingComplete(modelData.name, wallpaperPath, success ? cachedPath : "");
        });
      }

      // ------------------------------------------------------
      function requestPreprocessedWallpaper(originalPath) {
        // If we're already transitioning to this exact wallpaper, skip the request
        if (transitionLayer.transitioning && originalPath === transitionLayer.transitioningToOriginalPath) {
          return;
        }

        // Handle solid color paths - no preprocessing needed
        if (WallpaperService.isSolidColorPath(originalPath)) {
          transitionLayer.requestWallpaperTransition(originalPath, originalPath);
          WallpaperService.wallpaperProcessingComplete(modelData.name, originalPath, "");
          return;
        }

        const compositorScale = CompositorService.getDisplayScale(modelData.name);
        const targetWidth = Math.round(modelData.width * compositorScale);
        const targetHeight = Math.round(modelData.height * compositorScale);

        ImageCacheService.getLarge(originalPath, targetWidth, targetHeight, function (cachedPath, success) {
          // Ignore stale callback if we've moved on to a different wallpaper
          if (transitionLayer.transitioningToOriginalPath && originalPath !== transitionLayer.transitioningToOriginalPath) {
            return;
          }
          transitionLayer.requestWallpaperTransition(originalPath, success ? cachedPath : originalPath);
          // Pass cached path for blur optimization (already resized)
          WallpaperService.wallpaperProcessingComplete(modelData.name, originalPath, success ? cachedPath : "");
        });
      }
    }
  }
}
