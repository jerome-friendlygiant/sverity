// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A clka enable register that gates a clock (gclk = clka & en_q) and also directly gates a clkb reset must still be reported as RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, req, d, output logic q, qd);
  logic en_q, gclk, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) en_q <= 1'b0; else en_q <= req;
  assign gclk = clka & en_q;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) qd <= 1'b0; else qd <= d;
  assign r = rst_n & ~en_q;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
