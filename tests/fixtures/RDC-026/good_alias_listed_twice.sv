// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One control listed on both edges through an exact copy (`wire r = rst_n`) is one receiver with an undecided force: no set/clear conflict
// and no mixed-polarity users (RDC-028).
// expect-none: RDC-026 WARNING
// expect-none: RDC-028 ERROR
module top(input logic clka, rst_n, d, output logic q);
  wire r = rst_n;
  always_ff @(posedge clka or negedge rst_n or posedge r) if (!rst_n) q <= 1'b0; else if (r) q <= 1'b1; else q <= d;
endmodule
