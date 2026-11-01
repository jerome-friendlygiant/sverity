// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @FIXTURES@/CDC-045/cells.json
// the enable of a library clock gate is launched in clkb but the gate is clocked by clka (declared asynchronous)
// expect: CDC-020 ERROR
module ICG_LATCHED(input logic CLK, EN, output logic GCLK);
  assign GCLK = CLK & EN;
endmodule
module top(input logic clka, clkb, en_d, d, output logic q);
  logic en_q, g;
  always_ff @(posedge clkb) en_q <= en_d;
  ICG_LATCHED u_g(.CLK(clka), .EN(en_q), .GCLK(g));
  always_ff @(posedge g) q <= d;
endmodule
