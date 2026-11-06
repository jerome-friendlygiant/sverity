module two_stage_pipe (
  input  logic       clk,
  input  logic       reset_n,
  input  logic [7:0] input_data,
  output logic [7:0] output_data
);
  logic [7:0] stage_one;
  logic [7:0] stage_two;

  always_ff @(posedge clk) begin
    if (!reset_n) begin
      stage_one = 8'd0;
      stage_two = 8'd0;
    end else begin
      stage_one = input_data;
      stage_two = stage_one;
    end
  end

  assign output_data = stage_two;
endmodule

module top (
  input  logic       clk,
  input  logic       reset_n,
  input  logic [7:0] input_data,
  output logic [7:0] output_data
);
  two_stage_pipe u_pipe (
    .clk(clk),
    .reset_n(reset_n),
    .input_data(input_data),
    .output_data(output_data)
  );
endmodule
