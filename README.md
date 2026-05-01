# cv-dewarping-engine

휴대폰으로 촬영한 문서 이미지를 인식해서, 삐뚤어진 원근감을 보정하고 평평한 스캔 이미지처럼 펴 주는 C++ CLI 프로젝트입니다.

이 프로젝트는 macOS + VS Code 환경에서 시작하는 것을 기준으로 작성되어 있고, 나중에 JNI나 Objective-C++로 옮기기 쉽도록 `main()`과 비전 처리 로직을 분리해 두었습니다.

## What This Project Does

입력 이미지 1장을 받아서 아래 작업을 수행합니다.

1. 문서 가장자리를 찾습니다.
2. 문서의 4개 꼭짓점을 추정합니다.
3. 투시 변환으로 문서를 정면에서 본 것처럼 보정합니다.
4. 보정된 결과 이미지를 입력 파일명을 유지해서 `outputs/`에 저장합니다.
5. 원본 이미지 위에 얇은 외곽선과 꼭짓점 좌표를 표시한 마킹 이미지도 같은 이름 기반으로 함께 저장합니다.
6. 처리 결과를 JSON 문자열로 터미널에 출력합니다.

현재 검출 로직은 단순히 가장 큰 사각형만 고르는 방식이 아니라, 밝기와 내부 균일도를 함께 평가해서 책 외곽보다 실제 흰 페이지 영역을 더 우선하도록 조정되어 있습니다.
예를 들어 `inputs/input.jpg`를 넣으면 `outputs/input.jpg`와 `outputs/input_marked.jpg`가 생성됩니다.

## Project Structure

```text
cv-dewarping-engine/
├── include/
│   ├── document_scanner.hpp   # 비전 처리 로직의 공개 인터페이스
│   └── scanner_config.hpp     # 조정 가능한 설정 인터페이스
├── inputs/
│   └── .gitkeep               # 테스트용 입력 이미지를 넣는 폴더
├── outputs/
│   └── .gitkeep               # 보정된 결과 이미지가 저장되는 폴더
├── src/
│   ├── scanner_config.cpp     # 하드코딩된 설정값 관리
│   ├── document_scanner.cpp   # 문서 검출 + 투시 변환 로직
│   └── main.cpp               # CLI 진입점, 파일 입출력, JSON 출력
├── .gitignore
├── LICENSE
└── README.md
```

## Requirements

- macOS
- Apple Clang
- Homebrew
- OpenCV 4
- pkg-config

## 1. Install Dependencies

Homebrew가 아직 없다면 먼저 설치한 뒤, 아래 명령으로 필요한 패키지를 설치합니다.

```bash
brew install opencv pkg-config
```

설치가 잘 되었는지 확인하려면 아래 명령을 실행해 보세요.

```bash
pkg-config --modversion opencv4
```

예를 들어 `4.13.0`처럼 버전이 출력되면 정상입니다.

## 2. Build

프로젝트 루트에서 아래 명령을 실행합니다.

```bash
clang++ -std=c++17 -O2 \
  src/main.cpp src/scanner_config.cpp src/document_scanner.cpp \
  -Iinclude \
  $(pkg-config --cflags --libs opencv4) \
  -o scanner
```

정상적으로 빌드되면 현재 폴더에 `scanner` 실행 파일이 생성됩니다.

## VS Code Header Error

VS Code에서 아래 같은 에러가 보일 수 있습니다.

```text
파일 소스을(를) 열 수 없습니다. "opencv2/imgproc.hpp" C/C++(1696)
```

이 경우는 대부분 코드 문제보다 VS Code IntelliSense 설정 문제입니다.

이 프로젝트에는 이미 OpenCV 헤더 경로를 포함한 [.vscode/c_cpp_properties.json](/Users/newkimjiwon/project/cv-dewarping-engine/.vscode/c_cpp_properties.json:1)을 추가해 두었습니다.

그래도 에러가 남아 있으면 아래 순서로 확인해 보세요.

1. VS Code를 완전히 다시 엽니다.
2. Command Palette에서 `C/C++: Reset IntelliSense Database`를 실행합니다.
3. `pkg-config --cflags opencv4`가 정상 출력되는지 터미널에서 확인합니다.

현재 설정은 Apple Silicon Homebrew 기본 경로인 `/opt/homebrew/opt/opencv/include/opencv4`를 기준으로 잡혀 있습니다.

## VS Code Build

이 프로젝트에는 VS Code 빌드 작업도 추가되어 있습니다.

- `Cmd + Shift + B`: `Build scanner`
- Command Palette 또는 `Terminal > Run Task`에서 `Run scanner` 실행 가능

`Run scanner` 작업은 기본적으로 `inputs/input.jpg`를 사용합니다. 테스트 전에 해당 파일을 `inputs/` 폴더에 넣어 두면 바로 확인할 수 있습니다.

## 3. Prepare a Test Image

지금은 휴대폰으로 직접 테스트하지 않아도 되도록, 테스트 이미지를 `inputs/` 폴더에 넣어서 확인할 수 있게 해 두었습니다.

예를 들면:

```text
inputs/book-page.jpg
```

문서 사진은 아래 조건일수록 결과가 잘 나옵니다.

- 문서의 네 모서리가 사진 안에 모두 보이는 이미지
- 배경과 문서 경계가 어느 정도 구분되는 이미지
- 너무 심하게 흔들리거나 어둡지 않은 이미지

## 4. Run

아래처럼 입력 이미지 경로를 인자로 넘겨 실행합니다.

```bash
./scanner inputs/book-page.jpg
```

성공하면:

- 보정된 결과 이미지가 입력 파일명 기준으로 `outputs/`에 저장됩니다.
- 원본 위에 얇은 외곽선과 꼭짓점 좌표가 표시된 이미지가 `<원본이름>_marked` 형식으로 `outputs/`에 저장됩니다.
- 터미널에는 JSON 결과가 출력됩니다.

예시:

```json
{"success":true,"message":"Document detected and warped successfully.","output_path":"outputs/input.jpg","marked_output_path":"outputs/input_marked.jpg","corners":[{"x":120.45,"y":85.10},{"x":980.22,"y":70.35},{"x":1015.80,"y":1420.44},{"x":95.77,"y":1452.19}]}
```

## 5. If Something Goes Wrong

### `pkg-config: command not found`

아직 `pkg-config`가 설치되지 않았거나, 셸이 새로고침되지 않은 상태일 수 있습니다.

```bash
brew install pkg-config
```

설치 후 터미널을 다시 열거나, 아래 명령으로 확인해 보세요.

```bash
which pkg-config
```

### OpenCV 관련 헤더를 찾지 못하는 경우

대부분은 OpenCV 또는 `pkg-config` 설치가 제대로 안 되었을 때 발생합니다.

아래 명령이 정상적으로 동작하는지 먼저 확인합니다.

```bash
pkg-config --cflags --libs opencv4
```

### 실행은 되지만 문서를 못 찾는 경우

현재 알고리즘은 아래 순서로 동작합니다.

- Grayscale
- Gaussian Blur
- Canny Edge Detection
- Contour Search
- Polygon Approximation
- Perspective Transform

그래서 아래 같은 이미지에서는 실패할 수 있습니다.

- 문서 경계가 배경과 너무 비슷한 경우
- 그림자나 반사가 심한 경우
- 종이 일부가 잘린 경우
- 4개 꼭짓점을 안정적으로 찾기 어려운 경우

## Output Behavior

이 프로그램은 성공 여부와 좌표를 JSON으로 출력합니다.

- `success`: 문서 검출 및 저장 성공 여부
- `message`: 처리 결과 메시지
- `output_path`: 저장된 결과 이미지 경로
- `marked_output_path`: 원본 마킹 이미지 경로
- `corners`: 검출된 문서 꼭짓점 4개 좌표

이 형식은 나중에 모바일 앱, JNI, Objective-C++, 또는 다른 상위 레이어에서 재사용하기 쉽게 유지하는 것이 목적입니다.

## Code Design Notes

- [src/main.cpp](/Users/newkimjiwon/project/cv-dewarping-engine/src/main.cpp:1): CLI 인자 처리, 이미지 로드/저장, JSON 출력 담당
- [src/scanner_config.cpp](/Users/newkimjiwon/project/cv-dewarping-engine/src/scanner_config.cpp:1): 검출, 마킹, 출력 경로, 흑백 스캔 처리에 쓰는 설정값 관리
- [src/document_scanner.cpp](/Users/newkimjiwon/project/cv-dewarping-engine/src/document_scanner.cpp:1): 문서 검출 및 투시 보정 담당
- [include/document_scanner.hpp](/Users/newkimjiwon/project/cv-dewarping-engine/include/document_scanner.hpp:1): 비전 모듈 인터페이스

튜닝이 필요할 때는 보통 [src/scanner_config.cpp](/Users/newkimjiwon/project/cv-dewarping-engine/src/scanner_config.cpp:1)의 값만 먼저 조정하면 됩니다.

즉, 나중에 모바일 환경으로 옮길 때는 `main.cpp`를 대체하고 `detectAndWarpDocument()`를 재사용하는 방향으로 가져가면 됩니다.
