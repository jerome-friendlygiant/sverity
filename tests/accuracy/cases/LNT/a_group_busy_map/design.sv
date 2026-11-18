// Maps eight per-requester busy flags onto four group flags and registers the group grant.
// Requesters 2k and 2k+1 share group k; a group is free when its flag is low.
// all_free is high when every group is free in the registered view.
// While hold is high the registered view is frozen.
module top (
  input  logic [7:0] req_busy,
  input  logic       clk,
  input  logic       rst_n,
  input  logic       hold,
  output logic [3:0] grp_free_q,
  output logic       all_free
);
  wire [3:0] grp_busy;

  for (genvar i = 0; i < 8; i++) begin : g_map
    assign grp_busy[i/2] = req_busy[i];
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) grp_free_q <= 4'hF;
    else if (!hold) grp_free_q <= ~grp_busy;
  end

  assign all_free = &grp_free_q;
endmodule
