// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
// expect-none: CDC-016 ERROR
// expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, load, en, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] data; logic v_a, v1, v2;
  always_ff @(posedge clka) begin data <= d; v_a <= load; end
  always_ff @(posedge clkb) {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb) q <= (v2 & en) ? data : q;
endmodule
