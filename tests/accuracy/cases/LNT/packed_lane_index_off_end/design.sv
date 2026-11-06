module top(input logic [2:0][7:0] lanes,
  output logic [7:0] selected);

  localparam int ACTIVE_LANES = 3;
  function automatic logic [7:0] last_byte(input logic [2:0][7:0] value);
    return value[ACTIVE_LANES];
  endfunction
  always_comb begin
    selected = last_byte(lanes);
  end

endmodule
