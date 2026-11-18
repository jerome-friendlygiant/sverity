// sverity-args: --top top
// A priority pick in a constant-bound for loop whose eligibility depends on the grant it makes: grant -> blocked -> eligible -> pick ->
// grant. The loop is unrolled, so the dependencies are bit for bit and the cycle is proven, not an approximate possibility.
// expect: LNT-012 ERROR 7
module top(input logic [3:0] req, output logic [3:0] grant);
  logic [3:0] blocked, eligible, pick;
  assign blocked = {4{|grant}} & ~grant;
  assign eligible = req & ~blocked;
  always_comb begin
    pick = '0;
    for (int i = 0; i < 4; i++)
      if (eligible[i] && (pick == '0)) pick[i] = 1'b1;
  end
  assign grant = pick;
endmodule
