// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Conflicting declaration initializer (1'b1 vs guarded 1'b0) cannot be treated as constant-stuck.
// expect: CDC-013 WARNING
module top(input logic clka, clkb, en, output logic q);
  logic q_init = 1'b1;
  logic s1, s2;
  always_ff @(posedge clka) begin
    if (en) q_init <= 1'b0;
  end
  always_ff @(posedge clkb) {s2, s1} <= {s1, q_init};
  assign q = s2;
endmodule
