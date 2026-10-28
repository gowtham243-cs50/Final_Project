#!/usr/bin/env python3
import sys
import csv
import argparse

opcode_names = {0:"NOP",1:"READV",2:"WRITEV",3:"FSYNC",4:"READ_FIXED",5:"WRITE_FIXED",6:"POLL_ADD",7:"POLL_REMOVE",8:"SYNC_FILE_RANGE",9:"SENDMSG",10:"RECVMSG",11:"TIMEOUT",12:"TIMEOUT_REMOVE",13:"ACCEPT",14:"ASYNC_CANCEL",15:"LINK_TIMEOUT",16:"CONNECT",17:"FALLOCATE",18:"OPENAT",19:"CLOSE",20:"FILES_UPDATE",21:"STATX",22:"READ",23:"WRITE",24:"FADVISE",25:"MADVISE",26:"SEND",27:"RECV",28:"OPENAT2",29:"EPOLL_CTL",30:"SPLICE",31:"PROVIDE_BUFFERS",32:"REMOVE_BUFFERS",33:"TEE",34:"SHUTDOWN",35:"RENAMEAT",36:"UNLINKAT",37:"MKDIRAT",38:"SYMLINKAT",39:"LINKAT"}

class PolicyEngine:
    def __init__(self, block_opcode=None, max_len=None, deny_link_timeout=False):
        self.block_opcode = block_opcode
        self.max_len = max_len
        self.deny_link_timeout = deny_link_timeout
    
    def check(self, opcode, length):
        if self.deny_link_timeout and opcode == 15:  # LINK_TIMEOUT
            return False, "deny_link_timeout"
        if self.block_opcode is not None and opcode == self.block_opcode:
            return False, "block_opcode"
        if self.max_len is not None and opcode == 31 and length > self.max_len:  # PROVIDE_BUFFERS
            return False, "max_len_provide_buffers"
        if self.max_len is not None and opcode in (22, 23) and length > self.max_len:  # READ/WRITE
            return False, "max_len_read_write"
        return True, "allow"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--block-opcode', type=int)
    parser.add_argument('--max-len', type=int)
    parser.add_argument('--deny-link-timeout', action='store_true')
    parser.add_argument('--log', required=True)
    args = parser.parse_args()
    
    policy = PolicyEngine(args.block_opcode, args.max_len, args.deny_link_timeout)
    
    with open(args.log) as f:
        reader = csv.DictReader(f)
        for row in reader:
            opcode = int(row.get('opcode', row.get('opcode_num', 0)))
            length = int(row.get('len', row.get('length', 0)))
            allow, reason = policy.check(opcode, length)
            op_name = opcode_names.get(opcode, str(opcode))
            verdict = "ALLOW" if allow else "DENY"
            print(f"{verdict},{op_name}({opcode}),len={length},reason={reason},pid={row.get('pid','')},user_data={row.get('user_data','')}")

if __name__ == '__main__':
    main()
