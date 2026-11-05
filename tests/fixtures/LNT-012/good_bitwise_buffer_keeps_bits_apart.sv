// sverity-args: --top top
// Reduced from OpenTitan AES (prim_buf): a bitwise buffer shared by a request and an acknowledge of one handshake.
// Treating `~` as all-to-all made this a false ERROR; bit by bit there is no path from ack back to req.
// expect-none: LNT-012
module buf2(input logic [1:0] i, output logic [1:0] o);
  logic [1:0] inv;
  assign inv = ~i;
  assign o = ~inv;
endmodule
module top(input logic req_in, ack_in, output logic req_out, ack_out);
  logic [1:0] out;
  buf2 u(.i({ack_in & req_out, req_in}), .o(out));
  assign req_out = out[0];
  assign ack_out = out[1];
endmodule
