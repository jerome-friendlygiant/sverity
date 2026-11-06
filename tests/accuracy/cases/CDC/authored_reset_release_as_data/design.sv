module reset_release #(
  parameter int STAGES = 2
) (
  input logic clk_ctrl,
  input logic arst_n,
  output logic released
);
  logic [STAGES-1:0] pipe;
  always_ff @(posedge clk_ctrl or negedge arst_n) begin
    if (!arst_n) pipe <= '0;
    else pipe <= {pipe[STAGES-2:0], 1'b1};
  end
  assign released = pipe[STAGES-1];
endmodule

module data_engine #(
  parameter int WIDTH = 8
) (
  input logic clk_data,
  input logic arst_n,
  input logic release_ctrl,
  input logic [WIDTH-1:0] data_in,
  output logic [WIDTH-1:0] data_out,
  output logic active
);
  always_ff @(posedge clk_data or negedge arst_n) begin
    if (!arst_n) begin
      data_out <= '0;
      active <= 1'b0;
    end else if (release_ctrl) begin
      data_out <= data_in + 1'b1;
      active <= 1'b1;
    end else begin
      data_out <= '0;
      active <= 1'b0;
    end
  end
endmodule

module top (
  input logic clk_ctrl,
  input logic clk_data,
  input logic arst_n,
  input logic [7:0] data_in,
  output logic [7:0] data_out,
  output logic active
);
  logic release_ctrl;
  reset_release u_release (
    .clk_ctrl(clk_ctrl), .arst_n(arst_n), .released(release_ctrl)
  );
  data_engine #(.WIDTH(8)) u_engine (
    .clk_data(clk_data), .arst_n(arst_n), .release_ctrl(release_ctrl),
    .data_in(data_in), .data_out(data_out), .active(active)
  );
endmodule
