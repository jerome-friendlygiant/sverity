// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// the block's reset is the inverted copy of its source: the polarity changes across the hierarchy (INFO)
// expect: RDC-028 INFO
module top(input logic clka, rst_n, d, output logic q);
  logic rst;
  assign rst = ~rst_n;
  always_ff @(posedge clka or posedge rst) if (rst) q <= 1'b0; else q <= d;
endmodule
