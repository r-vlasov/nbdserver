image:
	mkdir ./tempdir
	mkdir ./tempdir/first
	mkdir ./tempdir/second
	echo "Hello world!" >> ./tempdir/first/text
	echo "How a u?" >> ./tempdir/first/text
	echo "HEY! NBD?" >> ./tempdir/README
	mkisofs -o iso/image.iso ./tempdir
	rm -rf ./tempdir
	echo "testdir's iso is made"

compile:
	gcc *.c -o nbd_server

clean:
	rm -rf ./tempdir
	rm -rf *.o
	rm -rf *.gch
