module mux_stage (
  input  logic       select,
  input  logic [7:0] left_data,
  input  logic [7:0] right_data,
  input  logic [7:0] mask,
  output logic [7:0] result
);
  always_comb begin
    if (select)
      result <= left_data & mask;
    else
      result <= right_data & mask;
  end
endmodule

module top (
  input  logic       select,
  input  logic [7:0] left_data,
  input  logic [7:0] right_data,
  input  logic [7:0] mask,
  output logic [7:0] result
);
  mux_stage u_stage (
    .select(select),
    .left_data(left_data),
    .right_data(right_data),
    .mask(mask),
    .result(result)
  );
endmodule
