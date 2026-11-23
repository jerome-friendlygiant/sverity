// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Multiple registers in one procedural block sharing the same reconvergence kind and influences group into one CDC-010 finding.
// expect-count: CDC-010 WARNING 1
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic r1, r2);
  logic a_q, b_q, s1a, s2a, s1b, s2b;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {b_q, a_q} <= '0; else {b_q, a_q} <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin
    {s2a, s1a, s2b, s1b} <= '0;
  end else begin
    {s2a, s1a} <= {s1a, a_q};
    {s2b, s1b} <= {s1b, b_q};
  end
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin
      r1 <= 1'b0;
      r2 <= 1'b0;
    end else begin
      r1 <= s2a & s2b;
      r2 <= s2a | s2b;
    end
  end
endmodule
