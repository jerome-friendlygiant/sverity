// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: wire rst_c = rst ^ 1'b0; => wire rst_c = rst ^ 1'b1;
// revert-expect-none: RDC-028 ERROR
// Constant XOR-with-0 is an exact copy of active-high rst; using it on negedge is a polarity mix (RDC-028 ERROR and RDC-004 WARNING).
// expect: RDC-028 ERROR
// expect: RDC-004 WARNING
// expect-none: RDC-011
// expect-none: RDC-019
module top(input logic clka, rst, d, output logic q);
  logic s;
  wire rst_c = rst ^ 1'b0;
  always_ff @(posedge clka or posedge rst) if (rst) s <= 1'b0; else s <= d;
  always_ff @(posedge clka or negedge rst_c) if (!rst_c) q <= 1'b0; else q <= s;
endmodule
