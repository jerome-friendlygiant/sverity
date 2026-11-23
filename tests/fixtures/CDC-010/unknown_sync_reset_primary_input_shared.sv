// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Shared unconstrained synchronous reset declared before primary-input data ports does not preempt their launch provenance.
// expect: CDC-010 WARNING 16
// expect-unresolved: CDC-010 16
module top(input logic clka, clkb, rst_b, d_a, d_b, output logic y);
  logic s1a, s2a, s1b, s2b;
  always_ff @(posedge clkb) begin
    if (rst_b) begin
      {s2a, s1a} <= '0;
      {s2b, s1b} <= '0;
    end else begin
      {s2a, s1a} <= {s1a, d_a};
      {s2b, s1b} <= {s1b, d_b};
    end
  end
  always_ff @(posedge clkb) begin
    if (rst_b) y <= 1'b0;
    else y <= s2a & s2b;
  end
endmodule
