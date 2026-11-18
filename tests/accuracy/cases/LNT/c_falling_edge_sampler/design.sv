// Input sampler for an interface that launches data on the rising edge.
// All registers use the falling edge, giving half a cycle of setup margin.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] data_i,
  input  logic       valid_i,
  output logic [7:0] data_o,
  output logic       valid_o,
  output logic [7:0] sample_cnt
);
  always_ff @(negedge clk or negedge rst_n) begin
    if (!rst_n) begin
      data_o  <= 8'd0;
      valid_o <= 1'b0;
    end else begin
      valid_o <= valid_i;
      if (valid_i) data_o <= data_i;
    end
  end

  always_ff @(negedge clk or negedge rst_n) begin
    if (!rst_n)       sample_cnt <= 8'd0;
    else if (valid_i) sample_cnt <= sample_cnt + 8'd1;
  end
endmodule
