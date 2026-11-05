// sverity-args: --top top
// Lookalike of good_bitwise_buffer_keeps_bits_apart with a real loop on one bit: bitwise precision must not hide it.
// expect: LNT-012 ERROR
module buf2(input logic [1:0] i, output logic [1:0] o);
  logic [1:0] inv;
  assign inv = ~i;
  assign o = ~inv;
endmodule
module top(input logic req_in, ack_in, output logic req_out, ack_out);
  logic [1:0] out;
  buf2 u(.i({ack_in, req_in & out[0]}), .o(out));
  assign req_out = out[0];
  assign ack_out = out[1];
endmodule
