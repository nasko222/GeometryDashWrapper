# PerformanceTest5 update-path correlation HUD

PerformanceTest5 is based on PerformanceTest4 and adds `--update-hud` plus
`RUN_UPDATE_PATH_HUD.cmd`.

The diagnostic installs exact entry hooks for these ARM functions:

- `PlayLayer::update(float)`
- `PlayLayer::checkCollisions(float)`
- `PlayLayer::updateVisibility()`
- `PlayLayer::checkSpawnObjects()`
- `PlayLayer::updateCamera(float)`
- `PlayerObject::update(float)`
- `PlayerObject::updateJump(float)`
- `PlayerObject::collidedWithObject(float, GameObject*)`
- `GameObject::activateObject()`
- `GameObject::deactivateObject()`
- `cocos2d::CCScheduler::update(float)`
- `cocos2d::CCActionManager::update(float)`

The title and log update every second. A frame is classified ACTIVE when
`PlayLayer::update` executed and PAUSED/STATIC otherwise. The output also keeps
GL draw, vertex and import counts to correlate gameplay work with rendering.

This launcher is diagnostic and adds hook overhead. The normal launcher remains
unchanged.
