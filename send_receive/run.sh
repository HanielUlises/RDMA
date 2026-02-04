#!/bin/bash

gcc receive_request.c -o receive_request -libverbs
gcc send_request_remote_qp.c -o send_request_remote_qp -libverbs