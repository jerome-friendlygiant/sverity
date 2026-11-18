// sverity-args: --top top
// expect: LNT-036 WARNING
module top(output logic [7:0] y);
  logic [2:0] i;
  function automatic void set_i(); i=7; endfunction
  always_comb begin
    y=0;
    for(i=0;i<3'd4;i++) begin
      set_i();
      y=8'd1 << (i+3'd1);
      break;
    end
  end
endmodule
