// sverity-args: --top top --profile fpga
// Looks undecidable: the synchroniser stages live in two instances of one cell, and only the instance-local symbols carry
// the attribute. Attributes are read from the RTL symbols, so the rule still decides: s3 in top is an unmarked stage of a
// partially marked chain, so one WARNING. Case-insensitive spelling (async_reg) is part of the trigger.
// expect-count: RDC-030 WARNING 1
module stage(input logic clk, arst_n, d, output logic q);
  (* async_reg = "true" *) logic r;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) r <= 1'b0; else r <= d;
  assign q = r;
endmodule
module top(input logic clk, arst_n, d, output logic q);
  logic s1, s2, s3;
  stage u1(.clk(clk), .arst_n(arst_n), .d(1'b1), .q(s1));
  stage u2(.clk(clk), .arst_n(arst_n), .d(s1), .q(s2));
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) s3 <= 1'b0; else s3 <= s2;
  always_ff @(posedge clk or negedge s3) if (!s3) q <= 1'b0; else q <= d;
endmodule
