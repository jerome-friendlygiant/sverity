// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-010 WARNING
// Regression from the audit: reconvergence one register stage behind the synchronisers.
// Two independently synchronised clka controls reconverge one register after the synchronisers.
module top(input logic clka, clkb, input logic [1:0] d, input logic en, output logic y);
  logic a_q, b_q;
  logic s1a, s2a, s1b, s2b, ra;
  always_ff @(posedge clka) begin a_q <= d[0]; b_q <= d[1]; end
  always_ff @(posedge clkb) begin s1a <= a_q; s2a <= s1a; s1b <= b_q; s2b <= s1b; end
  always_ff @(posedge clkb) ra <= s2a & en;     // local pipeline/qualification register
  always_ff @(posedge clkb) y <= ra & s2b;      // reconvergence one cycle later
endmodule
