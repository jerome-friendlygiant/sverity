// sverity-args: --top top
// expect: RDC-004 WARNING
// expect-none: RDC-003 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Source register is reset directly by rst_a_n; the destination (same clock) is reset by s2, whose
// first stage is reset by rst_a_n but whose last stage is reset by an unrelated por_n.
// When rst_a_n asserts, src changes asynchronously while dst is NOT yet in reset: RDC-004 hazard.
module top(input logic clk, rst_a_n, por_n, d, output logic q);
  logic s1, s2, src;
  always_ff @(posedge clk or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clk or negedge por_n)   if (!por_n)   s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clk or negedge rst_a_n) if (!rst_a_n) src <= 1'b0; else src <= d;
  always_ff @(posedge clk or negedge s2)      if (!s2)      q  <= 1'b0; else q  <= src;
endmodule
