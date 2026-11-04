# 9. 확장 플러그인과 실행 패키지

[← 이전: 저작 도구](08-authoring-tools.md) · [목차](README.md)

네이티브 확장은 에디터 안에서 실행할 코드를 추가합니다. 실행 패키지는 저장한 프로젝트 장면과 필요한 런타임 파일을 별도 폴더로 모읍니다. 두 기능의 목적과 배포 범위는 다릅니다.

[기본 화면 그림의 ① 메뉴](02-screen-camera.md)에서 `Window`는 확장 창, `File`은 패키지 만들기의 진입점입니다. 시작 장면 확인은 [프로젝트 설정 그림의 ①](03-scene-browser.md)을 참고하세요.

## 네이티브 확장 예제 빌드

**진입 경로:** `Window → Extensions / hot reload` → `Extensions` 창.

먼저 저장소 최상위에서 예제를 수동 빌드합니다.

```sh
cmake -S examples/editor-plugin -B build/editor-plugin -DCMAKE_BUILD_TYPE=Debug
cmake --build build/editor-plugin --config Debug
```

macOS는 보통 `build/editor-plugin/jengine_example_plugin.dylib`, Windows의 다중 구성 빌드는 `build/editor-plugin/Debug/jengine_example_plugin.dll`을 만듭니다. 실제 출력 폴더를 확인하세요.

1. `Extensions → Library path`에 빌드한 파일의 전체 경로를 넣습니다.
2. `Load extension`을 누릅니다.
3. 플러그인 UI의 `Create cube`로 실제 문서 물체를 만들고 `Cube position`으로 이동해 봅니다.
4. `Watch and reload`를 켠 상태에서 소스를 고친 뒤 같은 CMake 빌드를 다시 실행합니다.
5. 파일 변경이 안정되면 `generation` 증가와 동작을 확인합니다. 필요하면 `Reload`, 종료하려면 `Unload`를 누릅니다.

**예상 결과:** 엔진을 종료하지 않고 새 공유 라이브러리 코드를 로드합니다. 원본을 직접 잠그지 않도록 임시 복사본을 로드합니다. ABI 확인·새 OnLoad·선택적 상태 복원에 실패하면 기존 정상 버전을 유지하고 오류를 표시합니다. 컴파일을 자동 실행하는 기능은 아닙니다.

플러그인은 버전 1 C ABI의 `Log`, `CreateEntity`, `SetTransform`, `Text`, `Button`, `DragFloat3`를 사용합니다. ImGui·엔진 C++ 객체·GPU 포인터를 ABI로 넘기지 않습니다. 콜백은 에디터 소유 스레드에서 실행하며 백그라운드 콜백은 지원하지 않습니다. 선택적인 SaveState/RestoreState 쌍은 최대 64 KiB 상태를 다룹니다.

신뢰하는 네이티브 라이브러리만 사용하세요. 같은 프로세스에서 실행되며 잘못된 메모리 접근을 격리하는 샌드박스가 아닙니다. 새 장면·프로젝트를 열면 현재 문서 ID를 쓰던 확장을 종료하므로 필요할 때 다시 로드합니다. 언로드 후에도 만든 물체는 일반 장면 데이터로 남고, 저장은 File → Save로 합니다.

구현 예제와 외부 의존성 배포 조건은 [플러그인 예제 README](../../examples/editor-plugin/README.md), 정확한 ABI 계약은 [기술 문서](../editor-authoring.md)를 참고하세요. 이 확장은 에디터 도구용이며 패키지에 게임플레이 플러그인이 자동으로 포함·실행된다는 뜻은 아닙니다.

## 이동 가능한 실행 폴더 만들기

**진입 경로:** 프로젝트를 연 상태에서 `File → Build package`.

1. Play를 Stop하고 프로젝트 안에 시작 장면을 `.jscene`으로 저장합니다.
2. `Window → Project settings → Startup scene`을 확인합니다. 패키지 빌드 전 현재 장면을 저장하는 동작이 시작 장면을 현재 저장 파일로 갱신할 수 있으므로 원하는 장면을 연 상태에서 진행하세요.
3. `File → Build package`를 선택합니다.
4. `Build package into new folder → Path`에 **아직 없는 새 출력 폴더**를 지정하고 `Apply`를 누릅니다.
5. 상태의 `Package ready: .../jengine-runtime.json` 또는 오류 메시지를 기다립니다.

**예상 결과:** 장면과 전이 의존 자산·메타데이터, 실행 파일, 셰이더·필수 리소스, 플랫폼 런타임 의존성과 시작 스크립트를 새 폴더에 모읍니다. 누락 파일·경로 충돌·해결하지 못한 라이브러리가 있으면 완성된 패키지로 게시하지 않고 실패를 표시합니다. 실제 빌드 도구와 DXC·드라이버 파일이 필요하며 소스 트리 전체를 임의로 복사하는 기능은 아닙니다.

Windows와 macOS를 각각 해당 플랫폼·아키텍처에서 빌드해야 합니다. 설치 프로그램·코드 서명·공증·타 운영체제용 교차 컴파일까지 수행하는 배포 시스템은 아닙니다.

## 패키지 실행과 CLI

패키지 폴더 전체를 옮긴 뒤 포함된 시작 스크립트를 실행합니다.

```sh
# macOS: 패키지 폴더에서
./run-jEngine.command
```

```powershell
# Windows: 패키지 폴더에서
.\run-jEngine.cmd
```

직접 실행할 때는 포함된 런타임 실행 파일에 매니페스트의 전체 경로를 넘길 수 있습니다.

```sh
./runtime/jEngine --runtime /absolute/path/Package/jengine-runtime.json
```

Windows는 `runtime/jEngine.exe`를 사용합니다. macOS의 DXC·MoltenVK 환경 경로를 함께 맞춰 주는 기본 진입점은 `run-jEngine.command`입니다. 런타임은 매니페스트 위치 기준으로 파일을 해석하고 지문을 검증한 뒤 시작 장면을 Play로 실행하며 저작 UI는 숨깁니다.

개발용 프로젝트 열기는 `--project /path/project.jproject`, 장면 지정은 `--scene /path/Scene.jscene`입니다. `--runtime`은 이 두 인수와 함께 사용하지 않습니다. 현재 패키지 생성은 File 메뉴이며 존재하지 않는 `--package` 빌드 명령을 사용하지 마세요.

[← 이전: 저작 도구](08-authoring-tools.md) · [목차로 돌아가기](README.md)
