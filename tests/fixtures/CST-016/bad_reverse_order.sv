// The declaration releases a before b, but the RTL releases a under b: a is the last stage of a two-stage chain reset by b,
// and b is a one-stage synchroniser of rst_n. CST-016 WARNING Established.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/bad_reverse_order.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-confidence: CST-016 established
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic b_rst, a1, a_rst;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_rst <= 1'b0; else b_rst <= 1'b1;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) a1 <= 1'b0; else a1 <= 1'b1;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) a_rst <= 1'b0; else a_rst <= a1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
