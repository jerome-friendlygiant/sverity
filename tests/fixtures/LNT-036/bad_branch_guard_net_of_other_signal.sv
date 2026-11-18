// sverity-args: --top top
// Lookalike of good_branch_guard_through_assigned_net: `done` compares another counter, so ctr_q = 3 still reaches the index.
// revert: (other_q >= N) => (ctr_q >= N)
// revert-expect-none: LNT-036 WARNING
// expect: LNT-036 WARNING 14
module top(input logic enable, inc, input logic [127:0] d, input logic [1:0] ctr_q, other_q,
           output logic [383:0] b);
  localparam int unsigned N = 3;
  logic done;
  assign done = (other_q >= N);
  always_comb begin
    b = '0;
    if (!enable || done) b = '0;
    else if (inc) b[(N - 1 - ctr_q) * 128 +: 128] = d;
  end
endmodule
