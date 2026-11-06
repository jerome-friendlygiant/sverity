module top (
  input  logic clk_i,
  input  logic reset_i,
  input  logic data_i,
  output logic data_o
);
  logic reset_ni;
  logic [0:0] release_q;

  reset_sync #(.DEPTH(1)) u_reset_sync (
    .clk_i(clk_i), .reset_i(reset_i), .reset_ni(reset_ni)
  );

  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) data_o <= 1'b0;
    else data_o <= data_i;
  end
endmodule

module reset_sync #(
  parameter int DEPTH = 2
) (
  input logic clk_i,
  input logic reset_i,
  output logic reset_ni
);
  logic [DEPTH-1:0] sync_q;
  integer idx;
  always_ff @(posedge clk_i or posedge reset_i) begin
    if (reset_i) sync_q <= '0;
    else begin
      sync_q[0] <= 1'b1;
      for (idx = 1; idx < DEPTH; idx = idx + 1)
        sync_q[idx] <= sync_q[idx-1];
    end
  end
  assign reset_ni = sync_q[DEPTH-1];
endmodule
