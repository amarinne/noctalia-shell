import QtQuick
import Quickshell
import qs.Commons
import qs.Services.UI

Item {
  id: root

  property string transitionType: "fade"
  property real transitionProgress: 0
  property bool isStartupTransition: false
  property bool wallpaperReady: false
  property string futureWallpaper: ""
  property string transitioningToOriginalPath: ""

  readonly property real edgeSmoothness: Settings.data.wallpaper.transitionEdgeSmoothness
  readonly property var allTransitions: WallpaperService.allTransitions
  readonly property bool transitioning: transitionAnimation.running
  readonly property real fillMode: WallpaperService.getFillModeUniform()
  readonly property vector4d fillColor: Qt.vector4d(Settings.data.wallpaper.fillColor.r, Settings.data.wallpaper.fillColor.g, Settings.data.wallpaper.fillColor.b, 1.0)
  readonly property string currentSource: currentWallpaper.source

  property bool isSolid1: false
  property bool isSolid2: false
  property color _solidColor1: Settings.data.wallpaper.solidColor
  property color _solidColor2: Settings.data.wallpaper.solidColor
  property vector4d solidColor1: Qt.vector4d(_solidColor1.r, _solidColor1.g, _solidColor1.b, 1.0)
  property vector4d solidColor2: Qt.vector4d(_solidColor2.r, _solidColor2.g, _solidColor2.b, 1.0)

  property real wipeDirection: 0
  property real discCenterX: 0.5
  property real discCenterY: 0.5
  property real stripesCount: 16
  property real stripesAngle: 0
  property real pixelateMaxBlockSize: 64.0
  property real honeycombCellSize: 0.04
  property real honeycombCenterX: 0.5
  property real honeycombCenterY: 0.5

  Timer {
    id: debounceTimer
    interval: 333
    running: false
    repeat: false
    onTriggered: changeWallpaper()
  }

  Timer {
    id: startupTransitionTimer
    interval: 100
    running: false
    repeat: false
    onTriggered: _executeStartupTransition()
  }

  Image {
    id: currentWallpaper

    source: ""
    smooth: true
    mipmap: false
    visible: false
    cache: true
    asynchronous: true
    onStatusChanged: {
      if (status === Image.Error) {
        Logger.w("Current wallpaper failed to load:", source);
      } else if (status === Image.Ready && !wallpaperReady) {
        wallpaperReady = true;
      }
    }
  }

  Image {
    id: nextWallpaper

    property bool pendingTransition: false

    source: ""
    smooth: true
    mipmap: false
    visible: false
    cache: false
    asynchronous: true
    onStatusChanged: {
      if (status === Image.Error) {
        Logger.w("Next wallpaper failed to load:", source);
        pendingTransition = false;
      } else if (status === Image.Ready) {
        if (!wallpaperReady) {
          wallpaperReady = true;
        }
        if (pendingTransition) {
          pendingTransition = false;
          currentWallpaper.asynchronous = false;
          transitionAnimation.start();
        }
      }
    }
  }

  Loader {
    id: shaderLoader
    anchors.fill: parent
    active: true

    sourceComponent: {
      switch (transitionType) {
      case "wipe":
        return wipeShaderComponent;
      case "disc":
        return discShaderComponent;
      case "stripes":
        return stripesShaderComponent;
      case "pixelate":
        return pixelateShaderComponent;
      case "honeycomb":
        return honeycombShaderComponent;
      case "fade":
      case "none":
      default:
        return fadeShaderComponent;
      }
    }
  }

  Component {
    id: fadeShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_fade.frag.qsb")
    }
  }

  Component {
    id: wipeShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real smoothness: root.edgeSmoothness
      property real direction: root.wipeDirection
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_wipe.frag.qsb")
    }
  }

  Component {
    id: discShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real smoothness: root.edgeSmoothness
      property real aspectRatio: root.width / root.height
      property real centerX: root.discCenterX
      property real centerY: root.discCenterY
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_disc.frag.qsb")
    }
  }

  Component {
    id: stripesShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real smoothness: root.edgeSmoothness
      property real aspectRatio: root.width / root.height
      property real stripeCount: root.stripesCount
      property real angle: root.stripesAngle
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_stripes.frag.qsb")
    }
  }

  Component {
    id: pixelateShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real maxBlockSize: root.pixelateMaxBlockSize
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_pixelate.frag.qsb")
    }
  }

  Component {
    id: honeycombShaderComponent
    ShaderEffect {
      anchors.fill: parent

      property variant source1: currentWallpaper
      property variant source2: nextWallpaper.status === Image.Ready ? nextWallpaper : currentWallpaper
      property real progress: root.transitionProgress
      property real cellSize: root.honeycombCellSize
      property real centerX: root.honeycombCenterX
      property real centerY: root.honeycombCenterY
      property real aspectRatio: root.width / root.height
      property real fillMode: root.fillMode
      property vector4d fillColor: root.fillColor
      property real imageWidth1: source1.sourceSize.width
      property real imageHeight1: source1.sourceSize.height
      property real imageWidth2: source2.sourceSize.width
      property real imageHeight2: source2.sourceSize.height
      property real screenWidth: width
      property real screenHeight: height
      property real isSolid1: root.isSolid1 ? 1.0 : 0.0
      property real isSolid2: root.isSolid2 ? 1.0 : 0.0
      property vector4d solidColor1: root.solidColor1
      property vector4d solidColor2: root.solidColor2

      fragmentShader: Qt.resolvedUrl(Quickshell.shellDir + "/Shaders/qsb/wp_honeycomb.frag.qsb")
    }
  }

  NumberAnimation {
    id: transitionAnimation
    target: root
    property: "transitionProgress"
    from: 0.0
    to: 1.0
    duration: Settings.data.wallpaper.transitionDuration
    easing.type: Easing.InOutCubic
    onFinished: {
      if (isStartupTransition) {
        isStartupTransition = false;
      }

      transitioningToOriginalPath = "";
      isSolid1 = isSolid2;
      _solidColor1 = _solidColor2;

      const tempSource = nextWallpaper.source;
      currentWallpaper.source = tempSource;
      transitionProgress = 0.0;

      Qt.callLater(() => {
                     nextWallpaper.source = "";
                     isSolid2 = false;
                     Qt.callLater(() => {
                                    currentWallpaper.asynchronous = true;
                                  });
                   });
    }
  }

  function pathStr(p) {
    var s = p.toString();
    if (s.startsWith("file://")) {
      return s.substring(7);
    }
    return s;
  }

  function initializeWallpaper(source, animateStartup) {
    if (!source) {
      return;
    }

    futureWallpaper = source;
    if (animateStartup) {
      performStartupTransition();
    } else {
      isStartupTransition = false;
      setWallpaperImmediate(futureWallpaper);
      transitioningToOriginalPath = "";
    }
  }

  function requestWallpaperTransition(originalPath, resolvedPath) {
    if (!originalPath) {
      return;
    }

    if (transitioning && originalPath === transitioningToOriginalPath) {
      return;
    }

    transitioningToOriginalPath = originalPath;
    futureWallpaper = resolvedPath || originalPath;

    if (!wallpaperReady) {
      setWallpaperImmediate(futureWallpaper);
      transitioningToOriginalPath = "";
      return;
    }

    if (!WallpaperService.isSolidColorPath(futureWallpaper) && pathStr(futureWallpaper) === pathStr(currentWallpaper.source)) {
      transitioningToOriginalPath = "";
      return;
    }

    debounceTimer.restart();
  }

  function setWallpaperImmediate(source) {
    transitionAnimation.stop();
    transitionProgress = 0.0;

    var isSolidSource = WallpaperService.isSolidColorPath(source);
    isSolid1 = isSolidSource;
    isSolid2 = false;

    if (isSolidSource) {
      _solidColor1 = WallpaperService.getSolidColor(source);
      currentWallpaper.source = "";
      nextWallpaper.source = "";
      if (!wallpaperReady) {
        wallpaperReady = true;
      }
      return;
    }

    nextWallpaper.source = "";
    nextWallpaper.sourceSize = undefined;
    currentWallpaper.source = "";

    Qt.callLater(() => {
                   currentWallpaper.source = source;
                 });
  }

  function setWallpaperWithTransition(source) {
    var isSolidSource = WallpaperService.isSolidColorPath(source);

    if (isSolidSource && isSolid1) {
      var newColor = WallpaperService.getSolidColor(source);
      if (newColor === _solidColor1.toString()) {
        return;
      }
    }

    if (!isSolidSource && pathStr(source) === pathStr(currentWallpaper.source)) {
      return;
    }

    if (transitioning && source === nextWallpaper.source) {
      return;
    }

    if (transitioning) {
      transitionAnimation.stop();
      transitionProgress = 0;
      isSolid1 = isSolid2;
      _solidColor1 = _solidColor2;
      const newCurrentSource = nextWallpaper.source;
      currentWallpaper.source = newCurrentSource;

      Qt.callLater(() => {
                     nextWallpaper.source = "";
                     isSolid2 = false;
                     Qt.callLater(() => {
                                    _startTransitionTo(source, isSolidSource);
                                  });
                   });
      return;
    }

    _startTransitionTo(source, isSolidSource);
  }

  function _startTransitionTo(source, isSolidSource) {
    isSolid2 = isSolidSource;

    if (isSolidSource) {
      _solidColor2 = WallpaperService.getSolidColor(source);
      nextWallpaper.source = "";
      if (!wallpaperReady) {
        wallpaperReady = true;
      }
      currentWallpaper.asynchronous = false;
      transitionAnimation.start();
    } else {
      nextWallpaper.source = source;
      if (nextWallpaper.status === Image.Ready) {
        if (!wallpaperReady) {
          wallpaperReady = true;
        }
        currentWallpaper.asynchronous = false;
        transitionAnimation.start();
      } else {
        nextWallpaper.pendingTransition = true;
      }
    }
  }

  function changeWallpaper() {
    var selected = Settings.data.wallpaper.transitionType;
    if (!selected || selected.length === 0) {
      transitionType = "none";
    } else if (selected.length === 1) {
      transitionType = selected[0];
    } else {
      var index = Math.floor(Math.random() * selected.length);
      transitionType = selected[index];
    }

    if (transitionType !== "none" && !allTransitions.includes(transitionType)) {
      transitionType = "fade";
    }

    switch (transitionType) {
    case "none":
      setWallpaperImmediate(futureWallpaper);
      transitioningToOriginalPath = "";
      break;
    case "wipe":
      wipeDirection = Math.random() * 4;
      setWallpaperWithTransition(futureWallpaper);
      break;
    case "disc":
      discCenterX = Math.random();
      discCenterY = Math.random();
      setWallpaperWithTransition(futureWallpaper);
      break;
    case "stripes":
      stripesCount = Math.round(Math.random() * 20 + 4);
      stripesAngle = Math.random() * 360;
      setWallpaperWithTransition(futureWallpaper);
      break;
    case "pixelate":
      pixelateMaxBlockSize = Math.round(Math.random() * 80 + 32);
      setWallpaperWithTransition(futureWallpaper);
      break;
    case "honeycomb":
      honeycombCellSize = Math.random() * 0.04 + 0.02;
      honeycombCenterX = Math.random();
      honeycombCenterY = Math.random();
      setWallpaperWithTransition(futureWallpaper);
      break;
    default:
      setWallpaperWithTransition(futureWallpaper);
      break;
    }
  }

  function performStartupTransition() {
    if (Settings.data.wallpaper.skipStartupTransition) {
      setWallpaperImmediate(futureWallpaper);
      isStartupTransition = false;
      return;
    }

    isStartupTransition = true;

    var selected = Settings.data.wallpaper.transitionType;
    if (!selected || selected.length === 0) {
      transitionType = "none";
    } else if (selected.length === 1) {
      transitionType = selected[0];
    } else {
      var index = Math.floor(Math.random() * selected.length);
      transitionType = selected[index];
    }

    if (transitionType !== "none" && !allTransitions.includes(transitionType)) {
      transitionType = "fade";
    }

    switch (transitionType) {
    case "wipe":
      wipeDirection = Math.random() * 4;
      break;
    case "disc":
      discCenterX = 0.5;
      discCenterY = 0.5;
      break;
    case "stripes":
      stripesCount = Math.round(Math.random() * 20 + 4);
      stripesAngle = Math.random() * 360;
      break;
    case "pixelate":
      pixelateMaxBlockSize = 64.0;
      break;
    case "honeycomb":
      honeycombCellSize = 0.04;
      honeycombCenterX = 0.5;
      honeycombCenterY = 0.5;
      break;
    }

    startupTransitionTimer.start();
  }

  function _executeStartupTransition() {
    if (transitionType === "none") {
      setWallpaperImmediate(futureWallpaper);
      isStartupTransition = false;
    } else {
      setWallpaperWithTransition(futureWallpaper);
    }
  }
}
