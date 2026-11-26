// sverity-args: --top top
// v[0] is cleared unconditionally while v[1] is written only under a nested if (mode): v[1] holds when mode==0, so
// its forced value is undecided ('?'), yielding Unresolved for RDC-010, RDC-020 and RDC-025 (round 4 A2).
// expect-unresolved: RDC-010
// expect-unresolved: RDC-020
// expect-unresolved: RDC-025
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, rst_n, mode, d, output logic q, y);
  logic [1:0] v;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      v[0] <= 1'b0;
      if (mode) v[1] <= 1'b0;
    end else v <= {d, d};
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= v[1];
  assign y = v[1];
endmodule
