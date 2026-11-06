module sparse_pipeline #(parameter int WIDTH = 16) (
  input logic clk_i,
  input logic rst_ni,
  input logic enable_i,
  input logic [WIDTH-1:0] data_i,
  output logic [WIDTH-1:0] data_o
);
  logic phase_q;
  logic [WIDTH-1:0] stage_a_q;
  logic [WIDTH-1:0] stage_b_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      phase_q <= 1'b0;
      stage_a_q <= '0;
      stage_b_q <= '0;
    end else begin
      phase_q <= ~phase_q;
      if (enable_i && !phase_q) begin
        stage_a_q <= data_i ^ {WIDTH{phase_q}};
        stage_b_q <= stage_a_q;
      end
    end
  end

  assign data_o = stage_b_q;
endmodule

module top (
  input logic clk_i,
  input logic rst_ni,
  input logic enable_i,
  input logic [15:0] data_i,
  output logic [15:0] data_o
);
  sparse_pipeline #(.WIDTH(16)) u_pipe (.*);
endmodule
