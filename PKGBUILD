# Maintainer: David Heinemeier Hansson <david@hey.com>

pkgname=omacut
pkgver=0.1.0
pkgrel=1
pkgdesc='Dead-simple video length trimmer built with Qt Quick and ffmpeg'
arch=('x86_64')
url='https://github.com/omacom-io/omacut'
license=('custom')
depends=('ffmpeg' 'qt6-base' 'qt6-declarative' 'qt6-multimedia' 'xdg-desktop-portal')
makedepends=('gcc' 'make' 'qt6-base' 'qt6-declarative' 'qt6-multimedia')
source=()
sha256sums=()

build() {
  cd "$startdir"
  ./bin/build
}

package() {
  cd "$startdir"
  install -Dm755 build/omacut "$pkgdir/usr/bin/omacut"
}
