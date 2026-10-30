from pathlib import Path
from tempfile import TemporaryDirectory
import hashlib
import unittest
from create_release_torrent import bencode, create_torrent, PIECE_BYTES


class TorrentTests(unittest.TestCase):
    def test_canonical_bep3_examples(self):
        self.assertEqual(bencode({b"spam":b"eggs",b"cow":b"moo"}),b"d3:cow3:moo4:spam4:eggse")
        self.assertEqual(bencode([b"spam",-3]),b"l4:spami-3ee")
        with self.assertRaises(TypeError): bencode({"text":b"not a byte key"})

    def test_piece_boundary_short_tail_and_single_file_metadata(self):
        with TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'Русский файл.zip';target=root/'release.torrent'
            data=b'a'*PIECE_BYTES+b'short tail';source.write_bytes(data)
            digest=hashlib.sha256(data).hexdigest()
            report=create_torrent(source,target,digest)
            expected=bencode({b"created by":b"EvilDeadCommunity release tools",b"nodes":[],b"info":{
                b"name":source.name.encode(),b"length":len(data),b"piece length":PIECE_BYTES,
                b"pieces":hashlib.sha1(data[:PIECE_BYTES]).digest()+hashlib.sha1(data[PIECE_BYTES:]).digest()}})
            self.assertEqual(target.read_bytes(),expected)
            self.assertEqual(report['pieces'],2)
            self.assertNotIn(b'announce',expected)
            self.assertFalse(report['private'])
            with self.assertRaises(FileExistsError):create_torrent(source,target,digest)

    def test_wrong_hash_and_empty_payload_write_nothing(self):
        with TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'game.zip';target=root/'game.torrent'
            for data,checksum in [(b'changed','0'*64),(b'',hashlib.sha256(b'').hexdigest())]:
                source.write_bytes(data)
                with self.assertRaises(ValueError):create_torrent(source,target,checksum)
                self.assertFalse(target.exists())


if __name__ == '__main__':unittest.main()
