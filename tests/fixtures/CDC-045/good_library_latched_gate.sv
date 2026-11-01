// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// ICG_LATCHED is declared with an enable latch inside the cell: INFO with a trust model, no warning
// expect: CDC-045 INFO
// expect-none: CDC-045 WARNING
// expect-none: CDC-020 WARNING
module ICG_LATCHED(input logic CLK, EN, output logic GCLK);
  assign GCLK = CLK & EN;
endmodule
module top(input logic clka, en_d, d, output logic q);
  logic en_q, g;
  always_ff @(posedge clka) en_q <= en_d;
  ICG_LATCHED u_g(.CLK(clka), .EN(en_q), .GCLK(g));
  always_ff @(posedge g) q <= d;
endmodule
