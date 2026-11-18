// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        lane_up_i,
  input  logic        lane_ready_i,
  output logic        link_train_o,
  output logic        link_active_o,
  output logic        link_error_o,
  output logic [15:0] error_count_o
);

  logic [1:0] link_state;
  logic [15:0] err_q;

  phy_link_monitor u_mon (
    .clk_i       (clk_i),
    .rst_ni      (rst_ni),
    .lane_up_i   (lane_up_i),
    .lane_ready_i(lane_ready_i),
    .state_o     (link_state)
  );

  always_comb begin
    link_train_o  = 1'b0;
    link_active_o = 1'b0;
    link_error_o  = 1'b0;
    unique case (link_state)
      2'b00: link_train_o  = 1'b1;
      2'b01: link_active_o = 1'b1;
      2'b10: link_error_o  = 1'b1;
    endcase
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)           err_q <= '0;
    else if (link_error_o) err_q <= err_q + 1'b1;
  end

  assign error_count_o = err_q;

endmodule
