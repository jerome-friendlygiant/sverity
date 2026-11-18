// Fixed-priority arbiter that holds off other requesters while a grant is active.
module top #(
  parameter int N = 4
) (
  input  logic [N-1:0] req,
  output logic [N-1:0] grant,
  output logic         busy
);
  logic [N-1:0] blocked;
  logic [N-1:0] eligible;
  logic [N-1:0] pick;

  assign blocked  = {N{|grant}} & ~grant;
  assign eligible = req & ~blocked;

  // lowest-numbered eligible requester wins
  always_comb begin
    pick = '0;
    for (int i = 0; i < N; i++)
      if (eligible[i] && (pick == '0))
        pick[i] = 1'b1;
  end

  assign grant = pick;
  assign busy  = |grant;
endmodule
