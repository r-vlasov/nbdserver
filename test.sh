qemu-system-x86_64 -drive driver=nbd,host=localhost,port=10802
qemu-img info nbd:localhost:10809
sudo qemu-system-x86_64 -cpu host -enable-kvm -drive file=nbd:localhost:10809
