module top(input logic signed [7:0] temperature,
  
           input logic [7:0] limit,
  output logic below_limit);

  logic signed [8:0] signed_temperature, signed_limit;
  always_comb begin
    signed_temperature = {temperature[7], temperature};
    signed_limit = $signed({1'b0, limit});
    below_limit = signed_temperature < signed_limit;
  end

endmodule
