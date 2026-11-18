// sverity-args: --top top
// Lookalike of good_branch_guard_through_assigned_net: the branch writes `idx` after the test, so the value the test saw is not the
// value the index reads.
// revert: (N - 1 - idx) => (N - idx)
// revert-expect-none: LNT-036 WARNING
// expect: LNT-036 WARNING 15
module top(input logic [127:0] d, input logic [1:0] ctr_q, output logic [383:0] b);
  localparam int unsigned N = 3;
  logic [1:0] idx;
  always_comb begin
    b = '0;
    idx = ctr_q;
    if (idx < N) begin
      idx = idx + 2'd1;
      b[(N - 1 - idx) * 128 +: 128] = d;
    end
  end
endmodule
