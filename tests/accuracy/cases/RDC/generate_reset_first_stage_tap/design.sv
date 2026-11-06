module top (
  input  logic clk_i,
  input  logic reset_ni,
  input  logic [1:0] request_i,
  output logic [1:0] grant_o
);
  logic [1:0] reset_pipe_q [0:1];
  logic [1:0] local_reset_ni;
  genvar g;

  generate
    for (g = 0; g < 2; g = g + 1) begin : gen_reset
      always_ff @(posedge clk_i or negedge reset_ni) begin
        if (!reset_ni) begin
          reset_pipe_q[g][0] <= 1'b0;
          reset_pipe_q[g][1] <= 1'b0;
        end else begin
          reset_pipe_q[g][0] <= 1'b1;
          reset_pipe_q[g][1] <= reset_pipe_q[g][0];
        end
      end
      // A copy-paste error selects the metastability-catching stage.
      assign local_reset_ni[g] = reset_pipe_q[g][0];
    end
  endgenerate

  always_ff @(posedge clk_i or negedge local_reset_ni[0]) begin
    if (!local_reset_ni[0]) grant_o[0] <= 1'b0;
    else grant_o[0] <= request_i[0];
  end
  always_ff @(posedge clk_i or negedge local_reset_ni[1]) begin
    if (!local_reset_ni[1]) grant_o[1] <= 1'b0;
    else grant_o[1] <= request_i[1];
  end
endmodule
