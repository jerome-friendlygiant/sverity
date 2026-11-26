// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A handshake output ready_o = rst_n & (cnt != 3) combines one reset with a functional signal and has no reset word in its name.
// expect-none: RDC-009
module top(input logic clka, rst_n, push, pop, output logic ready_o);
  logic [1:0] cnt;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) cnt <= '0; else cnt <= cnt + push - pop;
  assign ready_o = rst_n & (cnt != 2'd3);
endmodule
