// sverity-args: --top top --sdc @DIR@/good_sync_reset_sync_source_resolved.sdc
// Synchroniser stages guarded by a trusted synchronous reset timed Same to capture domain resolve the launch source.
// expect: CDC-010 WARNING
// expect-confidence: CDC-010 heuristic
module top(input logic clka, clkb, rst_b, input logic [1:0] d, output logic y);
  logic a_q, b_q, s1a, s2a, s1b, s2b;
  always_ff @(posedge clka) begin a_q <= d[0]; b_q <= d[1]; end
  always_ff @(posedge clkb) begin
    if (rst_b) begin
      {s2a, s1a} <= '0;
      {s2b, s1b} <= '0;
    end else begin
      {s2a, s1a} <= {s1a, a_q};
      {s2b, s1b} <= {s1b, b_q};
    end
  end
  always_ff @(posedge clkb) begin
    if (rst_b) y <= 1'b0;
    else y <= s2a & s2b;
  end
endmodule
