# 4. 배치와 Inspector

[← 이전: 프로젝트·씬·자산](03-scene-browser.md) · [목차](README.md) · [다음: SurfelGI →](05-surfelgi.md)

큐브 하나를 추가하고 변형·복제·되돌리기를 익힌 뒤 여러 물체를 그룹으로 묶어 보세요. 이 장은 기본 저작 화면을 기준으로 합니다.

![선택한 Coral box와 실제 Hierarchy·기즈모·Inspector](images/06-workspace.png)

1280×900 실제 GPU 실행 원본에 빨간 상자·번호를 더한 확대 주석본입니다. [원본 보기](images/raw/06-workspace.png)

이 그림에서 **① Add 메뉴**, **② Hierarchy**, **③ Scene의 기즈모**, **④ Inspector**를 순서대로 찾습니다. 그림의 Coral box는 예제 선택이며 자신의 큐브 이름과 ID는 달라도 됩니다.

## 추가하고 변형하기

**진입 경로:** 상단 `Add` 또는 `Hierarchy → + Add`.

1. **① Add 메뉴의 `Cube`**를 선택합니다. 새 물체는 **② Hierarchy**에 추가되고 자동 선택됩니다.
2. **④ `Inspector → Name`**에 알아보기 쉬운 이름을 넣습니다.
3. **④ `Transform → Position`**에서 위치, `Rotation (deg)`에서 각도, `Scale`에서 배율을 조절합니다. 이 값은 부모를 기준으로 하는 로컬 값입니다.
4. `Tool`을 `Move`, `Rotate`, `Scale`로 바꾸고 **③ Scene**의 기즈모를 드래그합니다.
5. `Local axes`로 로컬/월드 축을 바꾸고, 필요한 경우 `Snap`과 `Move snap`을 켭니다.
6. `Edit → Undo`로 방금 변형을 되돌린 뒤 `Redo`로 다시 적용합니다.

**예상 결과:** 물체가 실제 장면에서 바뀌며 숫자와 기즈모가 같은 저작 데이터를 편집합니다. `Reset transform`은 로컬 위치·회전을 0, 배율을 1로 되돌립니다. 저장하려면 `File → Save`를 누릅니다.

`Add`에는 `Empty`, `Cube`, `Sphere`, 세 조명, `Camera`, `ParticleEmitter`, `Terrain`이 있습니다. 모델은 [Assets](03-scene-browser.md)에서 추가합니다. 새 루트 물체는 카메라 앞에 배치될 수 있으므로 `F`로 찾으세요.

## 선택·계층·가시성·잠금

Hierarchy 항목이나 Scene의 물체를 클릭하면 선택합니다. `Ctrl`/macOS `Command`를 누른 채 Hierarchy 항목을 클릭하면 선택을 추가합니다. 검색은 이름과 일치하는 항목 및 그 조상을 보여주며, 숨겨진 선택을 다시 찾을 때는 검색을 지우세요.

1. `Add → Empty`를 만들고 이름을 `Furniture`로 바꿉니다.
2. 기존 큐브를 Hierarchy의 `Furniture` 위로 드래그합니다.
3. 부모 `Furniture`를 이동하거나 회전해 자식이 상대 배치를 유지하는지 확인합니다.
4. 자식을 `Scene root` 위로 끌면 루트로 옮깁니다.

부모 변경은 가능한 범위에서 현재 월드 배치를 보존합니다. 계층 순환이나 로컬 TRS로 표현할 수 없는 변형은 오류로 거부합니다. 부모의 비균일 배율 아래에서 자식을 월드 축으로 회전시키는 경우처럼 전단이 필요한 조합은 그룹/균일 배율을 사용하세요.

`Visible`을 끄면 해당 물체·자식의 실제 렌더링과 선택 대상에서 빠집니다. Hierarchy의 기록은 남습니다. `Locked`는 편집·이동·복제·삭제를 막으며 부모 잠금은 자식에도 적용됩니다. 잠금 해제는 잠근 항목 자체에서 수행하세요.

## 복제·복사·삭제·Undo

| 조작 | 동작 |
| --- | --- |
| `Duplicate` / `Ctrl+D` | 선택한 루트들과 자식들을 새 안정 ID로 복제 |
| `Copy` / `Ctrl+C` | 선택한 여러 루트와 자식들을 클립보드에 복사 |
| `Paste` / `Ctrl+V` | 클립보드의 선택 묶음을 새 ID로 생성하고 선택 |
| `Delete` | 선택한 루트들과 자식 삭제 |
| `Undo` / `Ctrl+Z` | 마지막 문서 편집 되돌리기 |
| `Redo` / `Ctrl+Y`, `Ctrl+Shift+Z` | 되돌린 편집 다시 적용 |

여러 독립 항목을 선택해 Copy/Paste할 수 있습니다. 부모와 자식을 함께 선택하면 자식을 중복 복사하지 않으며, 복사한 루트의 월드 배치를 보존합니다. 선택 밖의 부모로부터 전단이나 애니메이션을 받아 독립 루트로 안전하게 옮길 수 없으면 오류로 거부합니다. 계속 하나의 묶음으로 다루려면 Empty 부모 아래 모아 그 부모를 선택하세요. Duplicate도 선택한 여러 루트와 자식을 중복 없이 처리합니다.

모델 복제는 메시·재질 핸들을 공유하고 위치·객체 ID는 따로 만듭니다. 인스턴스의 재질을 편집할 때는 별도 재질로 분리합니다. Undo/Redo는 현재 편집 세션의 명령 기록이며 파일을 다시 열면 이전 세션의 Undo 기록을 불러오지 않습니다. 자산 파일의 이동·휴지통 작업은 장면 Undo와 별도입니다.

## Prefab으로 묶음 재사용하기

1. Hierarchy에서 저장할 물체나 Empty 그룹 부모를 우클릭합니다.
2. `Save prefab`을 선택하고 프로젝트 안의 `Assets/Prefabs/Furniture.jprefab` 같은 경로를 입력합니다. 저장할 부모 폴더를 먼저 만들어 두세요.
3. `File → Instantiate prefab`에서 저장한 `.jprefab`을 지정하고 `Apply`를 누릅니다.
4. 생성된 새 ID와 자식 목록을 확인합니다.

**예상 결과:** 선택한 부모와 자식의 속성·상대 계층이 새 인스턴스로 복원됩니다. 원본 Prefab을 나중에 바꿨다고 배치된 모든 인스턴스가 자동 갱신되는 연결형 Prefab 시스템은 아닙니다.

## 재질·조명·카메라 속성

`Inspector → Material`에서 `Base color`, `Emissive`, `Metallic`, `Roughness`, `Opacity`와 텍스처 슬롯을 편집합니다. Assets의 텍스처를 `Base color texture`나 `Normal texture` 라벨 위로 끌어 배정합니다. 가져온 모델은 기본 원본 재질을 유지하며 `Override imported material` 또는 재질 속성 편집으로 덮어쓰기를 켭니다. 텍스처·재질 경로는 프로젝트 안에 있어야 합니다.

조명은 `Light`에서 `Color`, `Intensity`, `Range`, `Cast shadows`를 바꿉니다. SpotLight에는 `Inner cone`, `Outer cone`이 있습니다. 모든 조명 종류에서 같은 속성이 같은 효과를 내는 것은 아닙니다. 방향광에는 위치·거리 기반 감쇠가 없습니다.

Camera 물체는 `Vertical FOV`, `Near`, `Far`, `Primary`를 저장합니다. Play 중에는 보이는 주 카메라를 사용합니다. Scene의 자유 이동 카메라 위치를 저장하려면 Camera 엔티티의 Transform을 편집하세요. [저작 도구](08-authoring-tools.md)에서 재질 그래프와 애니메이션을 이어서 다룹니다.

## 서브메시 재질 편집

1. Hierarchy에서 가져온 Model을 펼치고 `Submesh 001` 같은 파트를 선택합니다. Scene에서 해당 파트를 클릭해 선택해도 됩니다.
2. Inspector에서 `Base color`, `Metallic`, `Roughness`, 텍스처, SSS·Skin 값을 수정합니다. 처음 수정하면 `Override submesh material`이 켜지고 선택한 모델 인스턴스의 해당 파트에만 적용됩니다.
3. Assets의 `.jmaterial`을 `Shared material`에 끌어 놓거나 `Material graph` 버튼으로 해당 파트의 노드 그래프를 엽니다. 아직 파트 전용 재질이 없으면 기본 그래프 저장 경로는 `Assets/Materials/Model_<ID>_Submesh_<번호>.jmaterial`입니다.
4. `Override submesh material`을 끄면 모델 전체 오버라이드가 있을 때는 그 재질을, 없으면 가져온 원본 재질을 다시 사용합니다.
5. 장면을 저장하면 파트별 재질도 함께 저장됩니다. Undo/Redo와 Prefab·복제에도 포함됩니다.

서브메시는 별도 장면 오브젝트를 만들지 않습니다. 변형·가시성·잠금은 부모 Model을 선택해 편집하세요. 잠긴 모델이나 Play 중에는 재질을 수정할 수 없습니다. 파트별 설정은 해당 모델의 메시 순서에 대응합니다. 다른 모델 자산으로 교체하면 기존 파트별 설정은 초기화되고, Undo로 모델과 설정을 함께 복구할 수 있습니다. 공유 `.jmaterial` 파일을 직접 수정하면 그 파일을 사용하는 다른 파트에도 반영됩니다.

## 자주 쓰는 장면 단축키

| 키 | 동작 |
| --- | --- |
| `Shift+W` / `Shift+E` / `Shift+R` | Move / Rotate / Scale |
| `Q` | 오른쪽 버튼을 누르지 않을 때 로컬/월드 축 전환 |
| `Ctrl+G` | 스냅 토글 |
| `F`, `Esc`, `Delete` | 초점 / 선택 해제 / 삭제 |
| `↑` / `↓` | 문서 항목 순서로 선택 이동 |
| `N` / `M` | 다음 추가 유형을 Cube / Sphere로 선택 |
| `L` / `K` / `J` | 다음 추가 유형을 Point / Spot / Directional로 선택 |
| `P` | 위에서 선택한 유형 추가 |

검색·숫자 입력·콘솔을 마친 뒤 Hierarchy나 Scene에 초점을 두고 누르세요. `Shift+W`가 카메라 전진으로 같이 처리되지 않도록 수정되어 있습니다. `F10`으로 클래식 렌더링 패널을 보더라도 저작 명령은 활성 상태입니다.

## 예전 Placement 그림과의 차이

![클래식 Placement Tool의 Add·검색·목록·Inspector](images/03-placement-inspector.png)

**클래식 Placement Tool 참고 그림입니다.** 1280×900 실제 실행 원본에 빨간 번호 주석을 추가했습니다. [원본 보기](images/raw/03-placement-inspector.png)

그림의 ①~③ 추가 유형·`Add`, ④~⑤ 검색·목록, ⑥ Inspector, ⑦ 조작 힌트는 이전 통합 탭의 위치입니다. 현재 기본 저작 화면에서는 같은 흐름을 `Add → Hierarchy → Inspector`에서 진행합니다. 위 단계에 그림 속 옛 좌표를 그대로 대입하지 마세요.

[← 이전: 프로젝트·씬·자산](03-scene-browser.md) · [목차](README.md) · [다음: SurfelGI →](05-surfelgi.md)
