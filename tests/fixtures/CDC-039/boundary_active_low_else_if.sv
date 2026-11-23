// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An active-low synchronized control gates the capture alongside a local enable.
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
// expect-none: CDC-028 ERROR
module top(input logic clka, clkb, load, din, en, output logic q);
  logic valid_n, v1, v2, data_q;
  always_ff @(posedge clka) begin valid_n <= ~load; data_q <= din; end
  always_ff @(posedge clkb) begin v1 <= valid_n; v2 <= v1; end
  always_ff @(posedge clkb) if (v2) q <= q; else if (en) q <= data_q;
endmodule
