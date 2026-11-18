// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top(input logic [3:0] x, output logic [3:0] y);
  parameter int DW = 1;
  logic [DW-1:0] dead;
  logic [DW-1:0] nested_dead;
  logic [DW-1:0] active_bad;
  if (DW == 4) begin : inactive
    assign dead = x;
    if (1) begin : nested
      assign nested_dead = x;
    end
  end
  assign active_bad = x;
  assign y = {dead, nested_dead, active_bad[0], x[0]};
endmodule
