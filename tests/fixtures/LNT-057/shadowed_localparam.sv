// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(input logic [15:0] a);
  logic [W-1:0] x;
  assign x=a;
  if (1) begin : g
    localparam W=8;
    logic [W-1:0] y;
    assign y=a;
  end
endmodule
