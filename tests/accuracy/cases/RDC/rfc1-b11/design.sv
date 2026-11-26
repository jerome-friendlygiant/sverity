// Counts valid beats once the bus interface is out of reset.
module top (
  input  logic clk,
  input  logic rst_ni,
  input  logic valid_i,
  output logic first_o
);
  logic [1:0] rst_sync_q;
  logic [7:0] count_q;

  always_ff @(posedge clk or negedge rst_ni)
    if (!rst_ni) rst_sync_q <= 2'b00;
    else         rst_sync_q <= {rst_sync_q[0], 1'b1};

  always_ff @(posedge clk or negedge rst_ni)
    if (!rst_ni) count_q <= '0;
    else if (valid_i && rst_sync_q[1]) count_q <= count_q + 8'd1;

  assign first_o = valid_i & rst_sync_q[1] & (count_q == 8'd0);
endmodule
