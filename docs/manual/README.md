# jEngine 사용설명서

jEngine의 기본 **저작 화면**에서 프로젝트를 만들고, 물체를 배치하고, 작성한 장면을 저장하는 순서로 안내합니다. 화면의 영문 라벨을 따라 조작하세요. 새 장면 형식은 `.jscene`, 프로젝트 설정 파일은 `project.jproject`입니다.

기본 화면은 `Hierarchy`, `Scene`, `Inspector`, `Project tools`로 구성됩니다. **장면 저장·열기, Undo/Redo, 복제, 부모·자식 계층, Prefab, 자산 가져오기와 Play/Stop을 사용할 수 있습니다.** 저장되는 장면 데이터와 실행 중인 미리보기의 범위는 각 장에서 구분합니다.

![실제 기본 저작 화면의 메뉴·계층·장면·Inspector·Project tools](images/06-workspace.png)

1280×900 실제 GPU 실행 원본에 빨간 상자·번호를 더한 확대 주석본입니다. [원본 보기](images/raw/06-workspace.png)

**① 메뉴와 Play → ② Hierarchy에서 선택 → ③ Scene에서 확인 → ④ Inspector에서 편집 → ⑤ Project tools에서 자산·로그 확인** 순서로 읽으세요. 각 그림의 번호는 새로 시작합니다. [저작 도구 그림](08-authoring-tools.md)과 [프로젝트 설정 그림](03-scene-browser.md)도 준비되어 있습니다.

## 처음이라면 이 순서로 읽으세요

| 순서 | 안내 | 여기서 할 일 |
| --- | --- | --- |
| 1 | [시작하기](01-getting-started.md) | 실행하고 프로젝트·첫 장면 저장하기 |
| 2 | [화면과 카메라](02-screen-camera.md) | 도킹 패널, 선택 초점, 이동 키 익히기 |
| 3 | [프로젝트·씬·자산](03-scene-browser.md) | 저장·복구, 모델 추가, 가져오기·재가져오기 사용하기 |
| 4 | [배치와 Inspector](04-placement-inspector.md) | 변형, 그룹, 복제, Undo, Prefab 사용하기 |
| 5 | [SurfelGI 살펴보기](05-surfelgi.md) | 렌더링 효과와 Inline GI 지원 조건 확인하기 |
| 6 | [콘솔과 로그](06-console-logs.md) | 명령 입력, 필터, 파일 로그 확인하기 |
| 7 | [문제 해결과 지원 범위](07-troubleshooting.md) | 저장·입력·자산·실행 문제 찾아보기 |
| 8 | [재질·애니메이션·효과·지형](08-authoring-tools.md) | 실제 저작 도구와 각 도구의 범위 익히기 |
| 9 | [확장 플러그인과 실행 패키지](09-extensions-packaging.md) | 네이티브 확장 빌드·교체, 이동 가능한 실행 폴더 만들기 |

## 가장 짧은 체험 순서

1. [시작하기](01-getting-started.md)의 자기 운영체제 명령으로 실행합니다.
2. `File → New project`에서 새 프로젝트 폴더와 이름을 입력합니다.
3. **① `Add → Cube`**를 선택하고 **② Hierarchy**의 새 큐브를 클릭합니다.
4. **④ `Inspector → Transform → Position`**을 바꾸고 `Ctrl+Z`로 되돌려 봅니다.
5. `File → Save As`에서 프로젝트 안의 `Scenes/First.jscene`으로 저장합니다.
6. `Play`로 미리 본 뒤 `Stop`으로 편집 상태에 돌아옵니다.

저작 화면에서 메뉴에 표시된 `Ctrl` 조합은 macOS의 `Command`로도 사용할 수 있습니다. 키 입력이 불편하면 같은 메뉴나 버튼을 사용하세요. 입력칸을 편집하는 동안에는 장면 단축키가 제한됩니다.

## 새 저작 화면과 클래식 그림 구분

새 그림 `06`~`08`은 저장소의 `examples/authoring-demo`를 정상 프로젝트 시작 경로로 실행한 화면입니다. 기본 도킹 화면, 실제 재질 그래프, 실제 프로젝트 설정을 촬영했습니다. 각 원본은 1280×900이며 글자 배율은 1.15입니다. 주석 도구가 만든 1495×1052 확대본은 UI 내용·라벨·수치를 원본과 대조했고, 원본 파일은 변경하지 않았습니다. 생성된 확대본의 픽셀이 원본과 동일하다고 주장하지 않습니다.

기존 빨간 번호 그림 `01`~`05`는 **F10으로 여는 클래식 렌더링 화면**을 촬영한 것입니다. 기본 저작 화면의 배치와 다르며, 새 Hierarchy·Project tools의 그림으로 해석하지 마세요. F10은 두 화면을 오가며, 저작 문서와 저작 명령은 유지됩니다.

번호는 그림마다 새로 시작합니다. 기존 그림은 1280×900 실제 실행 원본에 번호 주석을 추가했고, 가독성을 위해 확대되어 보일 수 있습니다. 그림 아래의 원본 링크로 비교할 수 있습니다. 장면 이름·ID·FPS는 사용자의 환경과 다를 수 있습니다.

기존 캡처는 Apple M4의 검증된 실험 MoltenVK 환경과 글자 배율 1.15를 사용했습니다. 이 캡처 기록을 모든 GPU나 새 기능 전체의 검증 결과로 일반화하지 마세요.

구현 범위와 기술 계약은 [Editor authoring technical guide](../editor-authoring.md), 빌드와 드라이버 준비는 [플랫폼 안내](../platform-and-ci.md), 측정 조건은 [성능 보고서](../reports/editor-authoring.md)를 참고하세요.

[첫 장으로: 시작하기 →](01-getting-started.md)
