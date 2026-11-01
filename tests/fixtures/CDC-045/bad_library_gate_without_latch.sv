// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// AND_GATE is declared without an enable latch: a posedge-launched enable can glitch the gated clock
// expect: CDC-045 WARNING
module AND_GATE(input logic CLK, EN, output logic GCLK);
  assign GCLK = CLK & EN;
endmodule
module top(input logic clka, en_d, d, output logic q);
  logic en_q, g;
  always_ff @(posedge clka) en_q <= en_d;
  AND_GATE u_g(.CLK(clka), .EN(en_q), .GCLK(g));
  always_ff @(posedge g) q <= d;
endmodule
