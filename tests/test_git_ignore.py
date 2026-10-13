"""使用 Git 的真实匹配结果，检查项目的密钥和依赖忽略规则。"""

import argparse
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import unittest


GIT = shutil.which("git") or r"D:\临时安装软件\git\cmd\git.exe"
REPOSITORY = Path(__file__).resolve().parents[1]
SECRET_PATHS = (
    ".env",
    ".env.local",
    ".env.production",
    "config/.env",
    "config/.env.test.local",
    ".git-credentials",
    "keys/id_rsa",
    "keys/id_ed25519",
    "keys/private.key",
    "keys/private.pem",
    "keys/certificate.p12",
    "keys/certificate.pfx",
)
DEPENDENCY_PATHS = ("node_modules/package/index.js", "app/node_modules/pkg/a.js")
# 用 --no-index 检查规则：即便文件已受 Git 管理，也能发现规则过宽的问题。
SOURCE_PATHS = ("README.md", "src/main.cpp", "include/robot_config.h", "tests/example.py")


def git(*arguments, input_bytes=None):
    result = subprocess.run(
        [GIT, "-C", str(REPOSITORY), *arguments],
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return result


def ignored_paths(paths):
    payload = b"\0".join(path.encode("utf-8") for path in paths) + b"\0"
    result = git("check-ignore", "--no-index", "--stdin", "-z", input_bytes=payload)
    return {item.decode("utf-8") for item in result.stdout.split(b"\0") if item}


class GitIgnoreTests(unittest.TestCase):
    def test_secret_files_are_ignored_at_root_and_in_subdirectories(self):
        self.assertEqual(set(SECRET_PATHS), ignored_paths(SECRET_PATHS))

    def test_dependency_directories_are_ignored(self):
        self.assertEqual(set(DEPENDENCY_PATHS), ignored_paths(DEPENDENCY_PATHS))

    def test_source_and_documentation_can_still_be_tracked(self):
        self.assertEqual(set(), ignored_paths(SOURCE_PATHS))

    def test_index_has_no_secret_files_or_node_dependencies(self):
        # .gitignore 不会移除已跟踪文件，因此单独检查索引，并只报告文件名。
        result = git("ls-files", "-z")
        self.assertEqual(0, result.returncode)
        forbidden = []
        for item in result.stdout.split(b"\0"):
            if not item:
                continue
            path = PurePosixPath(item.decode("utf-8"))
            name = path.name.lower()
            if (
                name == ".env"
                or name.startswith(".env.")
                or name in {".git-credentials", "id_rsa", "id_ed25519"}
                or path.suffix.lower() in {".key", ".pem", ".p12", ".pfx"}
                or "node_modules" in path.parts
            ):
                forbidden.append(str(path))
        self.assertEqual([], forbidden, "索引中存在应排除的文件；不要推送。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=REPOSITORY)
    arguments, unittest_arguments = parser.parse_known_args()
    REPOSITORY = arguments.repository.resolve(strict=True)
    if not (REPOSITORY / ".git").exists():
        parser.error("目标必须是已初始化的 Git 仓库。")
    unittest.main(argv=[sys.argv[0], *unittest_arguments])
